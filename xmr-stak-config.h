#ifndef XMR_STAK_CONFIG_H_
#define XMR_STAK_CONFIG_H_

#define DEFAULT_DELAY 75
#define LOG_FILE "xmr-stak-config.log"
#define STDOUT_FILE "xmr-stak-config.out"

#include <string>
#include <list>
#include <vector>
#include <set>
#include <map>

using namespace std;

typedef vector<int> power_options_t;

typedef set<string> configs_t;
typedef multimap<double, string> results_t;

class xmr_stak_config_t {
public:
	xmr_stak_config_t(const power_options_t& power_options, bool skip_prefetch);

	void print_configs();

	inline unsigned get_number_of_cores() const { return number_of_cores; }
	inline unsigned get_threads_per_core() const { return threads_per_core; }
	inline unsigned get_delay() const { return delay; }
	inline unsigned get_number_of_power_options() const { return power_options.size(); }
	inline unsigned get_number_of_configs() const { return configs.size(); }

	string get_estimated_runtime();
	void run();
	void finished();

	static string get_filename(const string& prefix);
	static string get_hashrate();
	static void verify_config();
private:
	void write_configs(list<int>& s, int threads);
	string get_config_line(int thread, const string& s);
	string get_config(const string& config);
	// sorted_results_t sort_results();

	void run_xmrstak(const string& config);

	results_t results;
	power_options_t power_options;
	configs_t configs;
	unsigned number_of_cores;
	unsigned threads_per_core;
	unsigned delay;
	string backup_filename;
	bool skip_prefetch;
};

#endif
