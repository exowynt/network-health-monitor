from flask import Flask, jsonify, request
import database  # our new module

app = Flask(__name__)

@app.route('/')
def index():
    return 'Hello, dashboard! (API is at /api/status)'

@app.route('/api/status')
def api_status():
    latest = database.get_latest_ping()
    outage_active = database.is_outage_active()

    # Determine status color based on latest ping latency
    status = 'green'
    if outage_active:
        status = 'red'
    elif latest is not None and latest['success']:
        latency = latest['latency_ms']
        if latency > 100:
            status = 'red'
        elif latency > 50:
            status = 'yellow'
        else:
            status = 'green'
    elif latest is not None and not latest['success']:
        # Most recent ping failed but we aren't in an outage (less than 3 consecutive fails)
        status = 'yellow'
    else:
        status = 'unknown'

    return jsonify({
        'current_status': status,
        'latest_ping': latest,
        'outage_active': outage_active
    })
@app.route('/api/history')
def api_history():
    # Get the range from query parameter, default to '1h'
    range_key = request.args.get('range', '1h')
    # Validate allowed values
    allowed = {'1h', '24h', '7d', '30d'}
    if range_key not in allowed:
        range_key = '1h'

    ping_data = database.get_ping_history(range_key)
    outages = database.get_outages_in_range(range_key)

    return jsonify({
        'range': range_key,
        'pings': ping_data,
        'outages': outages
    })
if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)
