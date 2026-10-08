"""
GPS Outlier Filtering + Parking Detection + History Simplification
Ported from server.js — filters implausible GPS jumps and detects stationary jitter.
"""

import math
from typing import List, Dict, Optional, Tuple


# ─── Constants ──────────────────────────────────────────
MAX_PLAUSIBLE_SPEED_KMH = 200
MIN_JUMP_METERS_TO_CHECK = 150
OUTLIER_RESET_GAP_MS = 3 * 60 * 1000  # 3 minutes

WEAK_SIGNAL_SATELLITES = 4
MIN_JUMP_METERS_TO_CHECK_WEAK = 60
MAX_PLAUSIBLE_SPEED_KMH_WEAK = 120

PARKING_JITTER_WINDOW = 6
PARKING_JITTER_RADIUS_M = 60
PARKING_MIN_SPAN_M = 12

HISTORY_SIMPLIFY_MIN_DISTANCE_M = 15
HISTORY_SIMPLIFY_MIN_TIME_MS = 15000
HISTORY_SIMPLIFY_SPEED_DELTA = 5


def haversine_distance(lat1: float, lng1: float, lat2: float, lng2: float) -> float:
    R = 6371e3
    phi1 = math.radians(lat1)
    phi2 = math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dlam = math.radians(lng2 - lng1)
    a = math.sin(dphi / 2) ** 2 + math.cos(phi1) * math.cos(phi2) * math.sin(dlam / 2) ** 2
    return R * 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a))


def get_signal_bars(satellites: int) -> int:
    if satellites <= 0:
        return 0
    if satellites < 4:
        return 1
    if satellites < 6:
        return 2
    if satellites < 9:
        return 3
    return 4


def is_weak_signal_point(point: Dict) -> bool:
    return (point.get("satellites") or 0) < WEAK_SIGNAL_SATELLITES


def is_implausible_jump(prev_point: Dict, candidate: Dict) -> bool:
    if not prev_point or not candidate:
        return False
    distance = haversine_distance(
        prev_point["lat"], prev_point["lng"],
        candidate["lat"], candidate["lng"]
    )
    weak_signal = is_weak_signal_point(prev_point) or is_weak_signal_point(candidate)
    min_jump = MIN_JUMP_METERS_TO_CHECK_WEAK if weak_signal else MIN_JUMP_METERS_TO_CHECK
    max_speed = MAX_PLAUSIBLE_SPEED_KMH_WEAK if weak_signal else MAX_PLAUSIBLE_SPEED_KMH

    if distance < min_jump:
        return False

    dt_seconds = max(1, ((candidate.get("timestamp") or 0) - (prev_point.get("timestamp") or 0)) / 1000)
    implied_speed_kmh = (distance / dt_seconds) * 3.6
    return implied_speed_kmh > max_speed


class ParkingDetector:
    def __init__(self):
        self._windows: Dict[str, List[Dict]] = {}

    def update(self, car_id: str, point: Dict) -> Optional[Dict]:
        if car_id not in self._windows:
            self._windows[car_id] = []
        win = self._windows[car_id]
        win.append(point)
        while len(win) > PARKING_JITTER_WINDOW:
            win.pop(0)
        return self._detect(car_id)

    def _detect(self, car_id: str) -> Optional[Dict]:
        win = self._windows.get(car_id)
        if not win or len(win) < PARKING_JITTER_WINDOW:
            return None
        if not all(is_weak_signal_point(p) for p in win):
            return None

        avg_lat = sum(p["lat"] for p in win) / len(win)
        avg_lng = sum(p["lng"] for p in win) / len(win)
        max_dist = 0
        total_span = 0
        for p in win:
            d = haversine_distance(avg_lat, avg_lng, p["lat"], p["lng"])
            if d > max_dist:
                max_dist = d
            total_span += d
        avg_span = total_span / len(win)

        if max_dist <= PARKING_JITTER_RADIUS_M and avg_span >= PARKING_MIN_SPAN_M:
            return {"lat": avg_lat, "lng": avg_lng, "radius": max(int(max_dist), 30)}
        return None


def filter_gps_outliers(points: List[Dict]) -> List[Dict]:
    if not points or len(points) < 2:
        return list(points) if points else []
    clean = [points[0]]
    last_good = points[0]
    for i in range(1, len(points)):
        candidate = points[i]
        if not candidate or not candidate.get("lat") or not candidate.get("lng"):
            continue
        gap_ms = (candidate.get("timestamp") or 0) - (last_good.get("timestamp") or 0)
        if is_implausible_jump(last_good, candidate) and gap_ms < OUTLIER_RESET_GAP_MS:
            continue
        clean.append(candidate)
        last_good = candidate
    return clean


def simplify_history_points(points: List[Dict]) -> List[Dict]:
    if not points or len(points) < 3:
        return list(points) if points else []
    result = [points[0]]
    last_kept = points[0]
    for i in range(1, len(points) - 1):
        p = points[i]
        if not p or p.get("lat") is None or p.get("lng") is None:
            continue
        dist = haversine_distance(last_kept["lat"], last_kept["lng"], p["lat"], p["lng"])
        dt = (p.get("timestamp") or 0) - (last_kept.get("timestamp") or 0)
        speed_delta = abs((p.get("speed") or 0) - (last_kept.get("speed") or 0))
        stopped_state_changed = ((last_kept.get("speed") or 0) < 1) != ((p.get("speed") or 0) < 1)
        if (
            dist < HISTORY_SIMPLIFY_MIN_DISTANCE_M
            and dt < HISTORY_SIMPLIFY_MIN_TIME_MS
            and speed_delta < HISTORY_SIMPLIFY_SPEED_DELTA
            and not stopped_state_changed
        ):
            continue
        result.append(p)
        last_kept = p
    result.append(points[-1])
    return result


# Singleton parking detector
parking_detector = ParkingDetector()
