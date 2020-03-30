/********************************************************************
 * Copyright (C) 2011 by Verimag                                    *
 * Initial author: Matthieu Moy                                     *
 ********************************************************************/

/*!
  \file measure-time.h
  \brief Utility to measure time


*/
#ifndef MEASURE_TIME_H
#define MEASURE_TIME_H

#include <iostream>

#ifdef __APPLE__
enum time_unit {sec, ms, us, ns, unspecified};
class measure_time { //IMPLEMENT C++11 BASED version using chrono

public:
	 measure_time(){}
		measure_time(std::string message, time_unit u = unspecified){
		}

	~measure_time(){}
};

#else

#define L_COUT std::cout /* Boucle for bizarre avec une seule instruction qui lock / unlock le flux */

// #include "io-lock.h"
#include <string>
#include <sstream>

enum time_unit {sec, ms, us, ns, unspecified};

static inline void display_resolution_maybe () {
	static int granularity_displaid = 0;
	if (!granularity_displaid) {
		struct timespec t;
		clock_getres(CLOCK_REALTIME, &t);

		L_COUT << "// TEST-IGNORE: granularity CLOCK_REALTIME = "
		       << (t.tv_sec * 1e9) + t.tv_nsec
		       << " nanoseconds" << std::endl;
		clock_getres(CLOCK_PROCESS_CPUTIME_ID, &t);

		L_COUT << "// TEST-IGNORE: granularity CLOCK_PROCESS_CPUTIME_ID = "
			       << (t.tv_sec * 1e9) + t.tv_nsec
		       << " nanoseconds" << std::endl;
		granularity_displaid = 1;
	}	
}

template<clockid_t ID>
class delta_time {
public:
	void start() {
		clock_gettime(ID, &m_start);
	}

	void stop() {
		clock_gettime(ID, &m_stop);
	}

	std::string to_string(time_unit u = unspecified) {
		std::stringstream s;
		double diff_sec = m_stop.tv_sec - m_start.tv_sec;
		double diff_nsec = m_stop.tv_nsec - m_start.tv_nsec;
		double diff = diff_sec + (diff_nsec/1e9);
		if (u == unspecified) {
			if (diff > 1) {
				u = sec;
			} else if (diff*1e3 > 1) {
				u = ms;
			} else if (diff*1e6 > 1) {
				u = us;
			} else {
				u = ns;
			}
		}
		switch (u) {
		case sec:
			s << diff << " sec";
			break;
		case ms:
			s << diff*1e3 << " ms";
			break;
		case us:
			s << diff*1e6 << " us";
			break;
		case ns:
			s << diff*1e9 << " ns";
			break;
		default: abort();
		}
		return s.str();
	}

private:
	struct timespec m_start;
	struct timespec m_stop;
};

/** Utility class to measure and display time on a portion of code */
class measure_time {
public:
	measure_time() : m_unit(unspecified)
		{
			start_measures();
		}
	measure_time(std::string message, time_unit u = unspecified) : 
		m_message(message),
		m_unit(u) {
		L_COUT << "// TEST-IGNORE: start measuring " + message << std::endl;
		start_measures();
	}

	~measure_time() {
		real.stop(); cpu.stop(); this_thread.stop();
		L_COUT << "// TEST-IGNORE: time[" << m_message << "] = "
		       << real.to_string(m_unit) << " real, "
		       << cpu.to_string(m_unit) << " CPU, "
		       << this_thread.to_string(m_unit) << " CPU (this thread)."
		       << std::endl;
	}

	void start_measures() {
		//display_resolution_maybe();
		real.start(); cpu.start(); this_thread.start();
	}


private:
	std::string m_message;
	time_unit m_unit;
	delta_time<CLOCK_REALTIME> real;
	delta_time<CLOCK_PROCESS_CPUTIME_ID> cpu;
	delta_time<CLOCK_THREAD_CPUTIME_ID> this_thread;
};

#endif // __APPLE__
#endif // MEASURE_TIME_H
