# SPDX-License-Identifier: AGPL-3.0-or-later
# ctypes binding to the Starboard C library (the flight code itself, not a
# re-implementation: Python gets bit-for-bit the answer a satellite gets).
import ctypes
import datetime as _dt
import glob
import os

_HERE = os.path.dirname(os.path.abspath(__file__))


def _load():
    names = glob.glob(os.path.join(_HERE, "_starboard*.so")) + \
        glob.glob(os.path.join(_HERE, "_starboard*.pyd")) + \
        glob.glob(os.path.join(_HERE, "_starboard*.dylib"))
    if not names:
        raise ImportError("Starboard C library not built: install with "
                          "`pip install .` from the python/ directory")
    return ctypes.CDLL(names[0])


class _Advice(ctypes.Structure):
    # Mirrors SbAdvice in starboard_easy.h; checked against
    # sb_easy_advice_size() at import.
    _fields_ = [
        ("action", ctypes.c_int),
        ("summary", ctypes.c_char * 256),
        ("self_id", ctypes.c_char * 13),
        ("other_id", ctypes.c_char * 13),
        ("other_name", ctypes.c_char * 40),
        ("conjunction_id", ctypes.c_uint64),
        ("miss_m", ctypes.c_double),
        ("pc", ctypes.c_double),
        ("tca_gps_ms", ctypes.c_int64),
        ("rule", ctypes.c_int),
        ("rule_text", ctypes.c_char_p),
        ("advisory", ctypes.c_int),
        ("burn_gps_ms", ctypes.c_int64),
        ("dv_rtn_mps", ctypes.c_double * 3),
        ("dv_mps", ctypes.c_double),
        ("return_dv_mps", ctypes.c_double),
        ("late", ctypes.c_int),
        ("error", ctypes.c_int),
        ("error_line", ctypes.c_uint32),
        ("error_text", ctypes.c_char_p),
    ]


lib = _load()
lib.sb_easy_decide.restype = ctypes.c_int
lib.sb_easy_decide.argtypes = [ctypes.c_char_p, ctypes.c_uint32,
                               ctypes.c_char_p, ctypes.c_int, ctypes.c_int64,
                               ctypes.POINTER(_Advice)]
lib.sb_easy_selftest.restype = ctypes.c_uint32
lib.sb_easy_selftest.argtypes = []
lib.sb_easy_advice_size.restype = ctypes.c_uint32
lib.sb_easy_profile_str.restype = ctypes.c_char_p
lib.sb_easy_profile_str.argtypes = [ctypes.c_int]
lib.sb_utc_to_gps_ms.restype = ctypes.c_int
lib.sb_utc_to_gps_ms.argtypes = [ctypes.c_int32] * 7 + \
    [ctypes.POINTER(ctypes.c_int64)]

if lib.sb_easy_advice_size() != ctypes.sizeof(_Advice):
    raise ImportError("Starboard binding/library layout mismatch: "
                      f"{ctypes.sizeof(_Advice)} != "
                      f"{lib.sb_easy_advice_size()} bytes")

PROFILES = tuple(lib.sb_easy_profile_str(i).decode() for i in range(6))

# GPS - UTC offset steps (seconds), from 1980-01-06. The C library pins the
# same table for UTC -> GPS (SPEC §3.3a).
_GPS_EPOCH = _dt.datetime(1980, 1, 6, tzinfo=_dt.timezone.utc)
_LEAPS = [(_dt.datetime(y, m, 1, tzinfo=_dt.timezone.utc), n)
          for y, m, n in [(1981, 7, 1), (1982, 7, 2), (1983, 7, 3),
                          (1985, 7, 4), (1988, 1, 5), (1990, 1, 6),
                          (1991, 1, 7), (1992, 7, 8), (1993, 7, 9),
                          (1994, 7, 10), (1996, 1, 11), (1997, 7, 12),
                          (1999, 1, 13), (2006, 1, 14), (2009, 1, 15),
                          (2012, 7, 16), (2015, 7, 17), (2017, 1, 18)]]


def gps_ms_to_utc(ms):
    """GPS milliseconds -> timezone-aware UTC datetime."""
    t = _GPS_EPOCH + _dt.timedelta(milliseconds=ms)
    leap = 0
    for when, n in _LEAPS:
        if t - _dt.timedelta(seconds=n) >= when:
            leap = n
    return t - _dt.timedelta(seconds=leap)


def utc_to_gps_ms(when):
    """UTC datetime (naive = UTC) -> GPS milliseconds, via the C library."""
    if when.tzinfo is not None:
        when = when.astimezone(_dt.timezone.utc)
    out = ctypes.c_int64()
    st = lib.sb_utc_to_gps_ms(when.year, when.month, when.day, when.hour,
                              when.minute, when.second,
                              when.microsecond // 1000, ctypes.byref(out))
    if st != 0:
        raise ValueError(f"time out of range: {when}")
    return out.value
