import sqlite3
import os

# Compute the absolute path to the database file.
# This script is in src/webapp/; project root is two levels up.
PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
DB_PATH = os.path.join(PROJECT_ROOT, 'data', 'network_health.db')

def get_connection():
    """Open a connection to the SQLite database."""
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row  # allows accessing columns by name
    return conn

def get_latest_ping():
    """Return the most recent ping result as a dict, or None if table empty."""
    conn = get_connection()
    cursor = conn.cursor()
    cursor.execute("SELECT * FROM ping_results ORDER BY id DESC LIMIT 1")
    row = cursor.fetchone()
    conn.close()
    if row is None:
        return None
    return dict(row)

def is_outage_active():
    """Return True if there's an outage with no end_time (ongoing)."""
    conn = get_connection()
    cursor = conn.cursor()
    cursor.execute("SELECT id FROM outages WHERE end_time IS NULL ORDER BY id DESC LIMIT 1")
    row = cursor.fetchone()
    conn.close()
    return row is not None

def get_recent_outages(limit=5):
    """Return the most recent completed outages."""
    conn = get_connection()
    cursor = conn.cursor()
    cursor.execute(
        "SELECT * FROM outages WHERE end_time IS NOT NULL ORDER BY id DESC LIMIT ?",
        (limit,)
    )
    rows = cursor.fetchall()
    conn.close()
    return [dict(r) for r in rows]
def get_ping_history(range_key):
    """
    Return aggregated ping data for the given range.
    range_key: '1h', '24h', '7d', '30d'
    Returns: list of dicts with keys depending on aggregation level.
    """
    ranges = {
        '1h':  ('-1 hour',   0),   # raw data, no aggregation
        '24h': ('-1 day',    60),  # 1-minute buckets
        '7d':  ('-7 days',   300), # 5-minute buckets
        '30d': ('-30 days',  900)  # 15-minute buckets
    }
    if range_key not in ranges:
        return []

    time_filter, bucket_seconds = ranges[range_key]
    conn = get_connection()
    cursor = conn.cursor()

    if bucket_seconds == 0:
        # Return raw pings for 1h (no aggregation)
        cursor.execute("""
            SELECT timestamp, latency_ms, success
            FROM ping_results
            WHERE timestamp >= datetime('now', ?)
            ORDER BY timestamp ASC
        """, (time_filter,))
        rows = cursor.fetchall()
        conn.close()
        return [{'timestamp': r['timestamp'],
                 'latency_ms': r['latency_ms'],
                 'success': r['success']} for r in rows]
    else:
        # Aggregate into time buckets
        cursor.execute(f"""
            SELECT
                datetime((strftime('%s', timestamp) / {bucket_seconds}) * {bucket_seconds}, 'unixepoch') as bucket,
                AVG(CASE WHEN success = 1 THEN latency_ms END) as avg_latency,
                MAX(CASE WHEN success = 1 THEN latency_ms END) as max_latency,
                SUM(CASE WHEN success = 0 THEN 1 ELSE 0 END) * 100.0 / COUNT(*) as packet_loss,
                COUNT(*) as sample_count
            FROM ping_results
            WHERE timestamp >= datetime('now', ?)
            GROUP BY bucket
            ORDER BY bucket ASC
        """, (time_filter,))
        rows = cursor.fetchall()
        conn.close()
        return [{'time_bucket': r['bucket'],
                 'avg_latency': round(r['avg_latency'], 2) if r['avg_latency'] is not None else None,
                 'max_latency': round(r['max_latency'], 2) if r['max_latency'] is not None else None,
                 'packet_loss': round(r['packet_loss'], 2),
                 'sample_count': r['sample_count']} for r in rows]
def get_outages_in_range(range_key):
    """Return outages that overlap the selected time range."""
    ranges = {
        '1h':  '-1 hour',
        '24h': '-1 day',
        '7d':  '-7 days',
        '30d': '-30 days'
    }
    if range_key not in ranges:
        return []

    time_filter = ranges[range_key]
    conn = get_connection()
    cursor = conn.cursor()
    # Outage overlaps if start_time is within range OR (start_time before range and end_time is NULL or within range)
    cursor.execute("""
        SELECT * FROM outages
        WHERE (start_time >= datetime('now', ?))
           OR (start_time < datetime('now', ?) AND (end_time IS NULL OR end_time >= datetime('now', ?)))
        ORDER BY start_time DESC
    """, (time_filter, time_filter, time_filter))
    rows = cursor.fetchall()
    conn.close()
    return [dict(r) for r in rows]
