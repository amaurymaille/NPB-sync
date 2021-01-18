#ifndef TIME_LOG_H
#define TIME_LOG_H

#include <string>

#include "nlohmann/json.hpp"

using json = nlohmann::json;

class TimeLog {
public:
    TimeLog(std::string const& synchronizer, std::string const& function) {
        _json["synchronizer"] = synchronizer;
        _json["function"] = function;
        _json["extras"] = json::object();
    }

    void add_time(unsigned int iteration, double time) {
        _json["times"][iteration] = time;
    }

    void add_time(unsigned int iteration, double time, json debug_data) {
        json data;
        data["time"] = time;
        data["debug"] = debug_data;

        _json["times"][iteration] = data;
    }

    void add_time_for_iteration(unsigned int iteration, unsigned int local_iteration, unsigned int thread_id, double time) {
        _json["times"]["iterations"][iteration]["local_iterations"][local_iteration]["threads"][thread_id] = time;
    }

    template<typename T>
    void add_extra_arg(const std::string& key, T const& value) {
        _json["extras"][key] = value;
    }

    const json& get_json() const {
        return _json;
    }

protected:
    json _json;
};

#endif // TIME_LOG_H
