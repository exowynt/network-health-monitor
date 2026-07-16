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
