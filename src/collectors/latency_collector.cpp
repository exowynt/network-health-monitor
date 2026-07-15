#include <iostream>
#include <string>
#include <cstdio>
#include <memory>
#include <array>
#include <chrono>
#include <thread>
#include <sqlite3.h>
#include <fstream>
#include <sstream>
#include <algorithm>
// ------------------------------------------------------------
// Execute a SQL statement that doesn't return data (INSERT, UPDATE, CREATE)
// ------------------------------------------------------------
void execute_sql(sqlite3* db, const std::string& sql) {
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << (err_msg ? err_msg : "unknown") << std::endl;
        sqlite3_free(err_msg);
    }
}

// ------------------------------------------------------------
// RAII wrapper to close a FILE* from popen
// ------------------------------------------------------------
struct PipeCloser {
    void operator()(FILE* f) const {
        if (f) pclose(f);
    }
};

// ------------------------------------------------------------
// Run a shell command and capture its output as a single string
// ------------------------------------------------------------
std::string exec_command(const std::string& cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, PipeCloser> pipe(popen(cmd.c_str(), "r"));
    if (!pipe) {
        std::cerr << "popen() failed!" << std::endl;
        return "";
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}
// ------------------------------------------------------------
// Parse ping output to extract latency (ms) and success status
// ------------------------------------------------------------
struct PingResult {
    double latency_ms;
    bool success;
};

PingResult parse_ping_output(const std::string& output) {
    PingResult res{0.0, false};

    // Look for the time= field (e.g., "time=14.2 ms")
    size_t pos = output.find("time=");
    if (pos != std::string::npos) {
        pos += 5; // skip "time="
        size_t end = output.find(" ms", pos);
        if (end != std::string::npos) {
            std::string latency_str = output.substr(pos, end - pos);
            try {
                res.latency_ms = std::stod(latency_str);
                res.success = true;
            } catch (...) {
                // parse failure: treat as unsuccessful
            }
        }
    }
    // If "time=" not found, success remains false
    return res;
}

// ------------------------------------------------------------
// Get current timestamp as ISO-8601 string (UTC)
// ------------------------------------------------------------
std::string now_utc() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm* tm = std::gmtime(&t);
    char buf[20];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", tm);
    return std::string(buf);
}
// Trim leading/trailing whitespace
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}
bool load_config(const std::string& filepath,
                 std::string& target,
                 int& interval_sec,
                 int& timeout_sec,
                 std::string& db_path) {
    std::ifstream config_file(filepath);
    if (!config_file.is_open()) {
        std::cerr << "Config file not found: " << filepath << ", using defaults." << std::endl;
        return false;
    }

    std::string line;
    while (std::getline(config_file, line)) {
        // Remove comments (everything after #)
        size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }
        // Skip empty lines
        line = trim(line);
        if (line.empty()) continue;

        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue; // invalid line

        std::string key = trim(line.substr(0, eq_pos));
        std::string value = trim(line.substr(eq_pos + 1));

        if (key == "target") target = value;
        else if (key == "interval_seconds") {
            try { interval_sec = std::stoi(value); } catch(...) {}
        }
        else if (key == "timeout_seconds") {
            try { timeout_sec = std::stoi(value); } catch(...) {}
        }
        else if (key == "db_path") db_path = value;
    }
    return true;
}
// ------------------------------------------------------------
// Main
// ------------------------------------------------------------
int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv; 
    // Default configuration
    std::string target = "8.8.8.8";
    int interval_sec = 10;
    int timeout_sec = 2;
    std::string db_path = "data/network_health.db";   // relative to build dir
    // Load configuration from file (falls back to defaults if missing)
    load_config("config/config.txt", target, interval_sec, timeout_sec, db_path);
    // Simple command-line overrides (skip arg parsing for clarity; we can add later)

    // Open SQLite database
    sqlite3* db = nullptr;
    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        return 1;
    }

    // Create tables if they don't exist
    execute_sql(db,
        "CREATE TABLE IF NOT EXISTS ping_results ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  target TEXT NOT NULL,"
        "  timestamp TEXT NOT NULL,"
        "  latency_ms REAL,"
        "  success INTEGER NOT NULL"
        ");"
    );
    execute_sql(db,
        "CREATE TABLE IF NOT EXISTS outages ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  start_time TEXT NOT NULL,"
        "  end_time TEXT"
        ");"
    );

    // Outage tracking state
    int consecutive_failures = 0;
    bool in_outage = false;

    std::cout << "Latency collector starting. Target: " << target
              << ", interval: " << interval_sec << "s" << std::endl;

    // Main loop
    while (true) {
        // 1. Run ping
        std::string cmd = "ping -c 1 -W " + std::to_string(timeout_sec) + " " + target;
        std::string output = exec_command(cmd);

        // 2. Parse result
        PingResult result = parse_ping_output(output);

        // 3. Insert into database
        std::string ts = now_utc();
        std::string sql = "INSERT INTO ping_results (target, timestamp, latency_ms, success) VALUES ('"
                        + target + "', '" + ts + "', "
                        + (result.success ? std::to_string(result.latency_ms) : "NULL") + ", "
                        + std::to_string(result.success ? 1 : 0) + ");";
        execute_sql(db, sql);

        // 4. Outage detection logic
        if (result.success) {
            // Successful ping
            if (in_outage) {
                // Outage just ended: update the latest outage with end_time
                std::string end_ts = ts;
                std::string update_sql = "UPDATE outages SET end_time = '" + end_ts
                                       + "' WHERE end_time IS NULL ORDER BY id DESC LIMIT 1;";
                execute_sql(db, update_sql);
                in_outage = false;
                std::cout << "Outage ended at " << end_ts << std::endl;
            }
            consecutive_failures = 0;
        } else {
            // Failed ping
            consecutive_failures++;
            std::cout << "Ping failed (" << consecutive_failures << " consecutive)" << std::endl;
            if (consecutive_failures >= 3 && !in_outage) {
                // Outage starts
                std::string start_ts = ts;
                std::string insert_sql = "INSERT INTO outages (start_time) VALUES ('" + start_ts + "');";
                execute_sql(db, insert_sql);
                in_outage = true;
                std::cout << "Outage started at " << start_ts << std::endl;
            }
        }

        // 5. Sleep for the remaining interval (simple approach)
        std::this_thread::sleep_for(std::chrono::seconds(interval_sec));
    }

    sqlite3_close(db);
    return 0;
}
