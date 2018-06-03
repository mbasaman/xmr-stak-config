#include "xmr-stak-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/prctl.h>

#include <iostream>
#include <fstream>
#include <thread>
#include <list>
#include <vector>
#include <set>
#include <regex>

#include <boost/thread/thread.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/filesystem.hpp>

using namespace std;

xmr_stak_config_t::xmr_stak_config_t(const power_options_t& power_options, bool skip_prefetch) :
		power_options(power_options),
		delay(DEFAULT_DELAY),
		skip_prefetch(skip_prefetch) {

	number_of_cores = boost::thread::physical_concurrency();

	if(number_of_cores) {
		threads_per_core = boost::thread::hardware_concurrency() / number_of_cores;
	} else {
		threads_per_core = 0;
	}

	int threads(threads_per_core);

	while(threads > 0) {
		list<int> tmp;
		write_configs(tmp, threads);
		threads--;
	}

	backup_filename = get_filename("cpu.bak");

	ifstream cpu_config("cpu.txt", ios::binary);
	ofstream backup_file(backup_filename.c_str(), ios::binary);

	backup_file << cpu_config.rdbuf();

	cpu_config.close();
	backup_file.close();

	cout << "saved cpu.txt to " << backup_filename << endl;
}

void xmr_stak_config_t::write_configs(list<int>& tmp, int threads) {
	if(threads > 0) {
		threads--;

		for(int i = 0; i < power_options.size() * (skip_prefetch ? 1 : 2); i++) {
			list<int> current(tmp);
			current.push_back(i);
			write_configs(current, threads);
		}
	} else {
		tmp.sort();

		stringstream ss;
		for(list<int>::iterator itr = tmp.begin(); itr != tmp.end(); itr++) {
			if(*itr < 100) {
				ss << "0";
			}

			if(*itr < 10) {
				ss << "0";
			}

			ss << *itr << " ";
		}

		configs.insert(ss.str());
	}
}

void xmr_stak_config_t::print_configs() {
	for(configs_t::iterator config = configs.begin(); config != configs.end(); config++) {
		cout << *config << endl;
		cout << get_config(*config) << endl;
	}

	cout << configs.size() << " configs" << endl;
}

string xmr_stak_config_t::get_config_line(int thread, const string& s) {
	int option = atoi(s.c_str());

	stringstream ss;

	ss << "{ \"low_power_mode\" : ";
	ss << power_options.at(option % power_options.size());
	ss << ", \"no_prefetch\" : ";
	ss << (option >= power_options.size() ? "true" : "false");
	ss << ", \"affine_to_cpu\" : ";
	ss << thread;
	ss << "},";

	return ss.str();
}

string xmr_stak_config_t::get_config(const string& config) {
	istringstream iss(config);

	vector<string> tokens{istream_iterator<string>{iss}, istream_iterator<string>{}};

	stringstream ss;

	ss << "\"cpu_threads_conf\" :" << endl;
	ss << "[" << endl;

	for(int i = 0; i < number_of_cores; i++) {
		int thread(i * threads_per_core);

		for(vector<string>::iterator itr = tokens.begin(); itr != tokens.end(); itr++) {
			ss << get_config_line(thread++, *itr) << endl;
		}
	}

	ss << "]," << endl;

	return ss.str();
}

string xmr_stak_config_t::get_filename(const string& prefix) {
	for(int i = 1; i < 1000; i++) {
		stringstream filename;
		filename << prefix << "." << i << ".txt";

		boost::filesystem::path path(filename.str());

		if(!exists(path)) {
			return filename.str();
		}
	}

	cout << "ERROR: failed to create file with prefix: " << prefix << endl;
	exit(1);

	return "";
}

string xmr_stak_config_t::get_hashrate() {
	std::ifstream ifs("xmr-stak-config.out");

	std::regex e ("Totals\\ \\(ALL\\):\\ +([0-9.]+)\\ +([0-9.]+)\\ +([0-9.]+)\\ +H\\/s");

	string ret("0.0");

	string line;
	while(getline(ifs, line)) {
		smatch match;

		regex_match(line, match, e);

		if(match.size() == 4) {
			ret = match[2].str();
		}
	}

	return ret;
}

void xmr_stak_config_t::finished() {
	std::ifstream backup_file(backup_filename.c_str(), ios::binary);
	std::ofstream cpu_config("cpu.txt", ios::binary | ios::trunc);

	cpu_config << backup_file.rdbuf();

	cpu_config.close();
	backup_file.close();

	unlink(backup_filename.c_str());

	cout << "restored cpu.txt" << endl;

	if(results.size()) {
		string results_filename = xmr_stak_config_t::get_filename("results");

		std::ofstream results_ofs(results_filename.c_str(), ios::app);

		for(results_t::reverse_iterator itr = results.rbegin(); itr != results.rend(); itr++) {
			cout << itr->second << endl;
			results_ofs << itr->second << endl;

			cout << "Hashrate: " << itr->first << endl << endl;
			results_ofs << "Hashrate: " << itr->first << endl << endl;
		}

		cout << "wrote results to " << results_filename << endl;

		results_ofs.close();
	}

	unlink(STDOUT_FILE);
}

void xmr_stak_config_t::run_xmrstak(const string& config) {
	ofstream output(LOG_FILE, ios::app);
	ofstream cpu_config("cpu.txt", ios::binary | ios::trunc);

	string config_json = get_config(config);

	cout << config_json << endl;
	output << config_json << endl;
	cpu_config << config_json << endl;

	cpu_config.close();

	pid_t pid = fork();

	if(pid == 0) {
		prctl(PR_SET_PDEATHSIG, SIGKILL);

		int stdout_fd = open(STDOUT_FILE, O_WRONLY|O_CREAT|O_TRUNC, 0644);

		dup2(stdout_fd, STDOUT_FILENO);

		close(stdout_fd);

		execl("xmr-stak", "xmr-stak", NULL);

		cout << "execl failed" << endl;
		exit(1);
	}

	sleep(delay);

	string hashrate = get_hashrate();

	int timeout(0);

	while(hashrate == "0.0" && timeout < delay) {
		sleep(5);
		timeout += 5;
		hashrate = get_hashrate();
	}

	kill(pid, SIGKILL);

	int result;
	waitpid(pid, &result, 0);

	if(hashrate == "0.0") {
		cout << "Couldn't find hashrate" << endl << endl;
		output << "Couldn't find hashrate" << endl << endl;
	} else {
		cout << "Hashrate: " << hashrate << endl << endl;
		output << "Hashrate: " << hashrate << endl << endl;
	}

	results.insert(make_pair(atof(hashrate.c_str()), config_json));
}

void xmr_stak_config_t::verify_config() {
	bool config_ok(true);

	boost::filesystem::path xmr_path("xmr-stak");

	if(!exists(xmr_path)) {
		cout << "xmr-stak not found" << endl;
		config_ok = false;
	}

	boost::filesystem::path config_path("config.txt");

	if(!exists(config_path)) {
		cout << "config.txt not found" << endl;
		config_ok = false;
	}

	boost::filesystem::path pools_path("pools.txt");

	if(!exists(pools_path)) {
		cout << "pools.txt not found" << endl;
		config_ok = false;
	}

	if(!config_ok) {
		exit(1);
	}

	std::ifstream config_in("config.txt");
	string config_line;

	string daemon_mode;
	string flush_stdout;
	string verbose_level;
	string h_print_time;

	// I'll add jsoncpp later

	std::regex e1("\\ *\"daemon_mode\"\\ *:\\ *([a-z]+),\\ *");
	std::regex e2("\"flush_stdout\"\\ *:\\ *([a-z]+),\\ *");
	std::regex e3("\"verbose_level\"\\ *:\\ *([0-9]+),\\ *");
	std::regex e4("\"h_print_time\"\\ *:\\ *([0-9]+),\\ *");

	while(getline(config_in, config_line)) {
		smatch match1;
		smatch match2;
		smatch match3;
		smatch match4;

		regex_match(config_line, match1, e1);
		regex_match(config_line, match2, e2);
		regex_match(config_line, match3, e3);
		regex_match(config_line, match4, e4);

		if(match1.size() == 2) {
			daemon_mode = match1[1].str();
		}

		if(match2.size() == 2) {
			flush_stdout = match2[1].str();
		}

		if(match3.size() == 2) {
			verbose_level = match3[1].str();
		}

		if(match4.size() == 2) {
			h_print_time = match4[1].str();
		}
	}

	config_in.close();


	if(daemon_mode != "true") {
		cout << "\"daemon_mode = true\" is required" << endl;
		config_ok = false;
	}

	if(flush_stdout != "true") {
		cout << "\"flush_stdout = true\" is required" << endl;
		config_ok = false;
	}

	if(verbose_level != "4") {
		cout << "\"verbose_level = 4\" is required" << endl;
		config_ok = false;
	}

	if(h_print_time != "75") {
		cout << "\"h_print_time = 75\" is required" << endl;
		config_ok = false;
	}

	if(!config_ok) {
		cout << endl << "adjust config.txt" << endl;
		exit(1);
	}
}

string xmr_stak_config_t::get_estimated_runtime() {
	int elapsed = configs.size() * delay;
	int hours(0);
	int minutes(0);

	while(elapsed >= 3600) {
		hours++;
		elapsed -= 3600;
	}

	while(elapsed >= 60) {
		minutes++;
		elapsed -= 60;
	}

	char buffer[100];
	memset(buffer, 0, 100);
	sprintf(buffer, "%d:%02d:%02d", hours, minutes, elapsed);

	return string(buffer);
}

void xmr_stak_config_t::run() {
	for(configs_t::iterator itr = configs.begin(); itr != configs.end(); itr++) {
		run_xmrstak(*itr);
	}

	finished();
}

int main() {
	xmr_stak_config_t::verify_config();

	string in;
	bool skip_prefetch(false);

	while(1) {
		cout << endl << "skip tests for no_prefetch: [yN]? ";
		cin >> in;

		if(in == "Y" || in == "y") {
			skip_prefetch = true;
			break;
		}

		if(in == "N" || in == "n") {
			break;
		}
	}

	power_options_t power_options;

	boost::filesystem::path power_options_file("power_options.txt");

	if(!exists(power_options_file)) {
		power_options.push_back(1);
		power_options.push_back(2);
		power_options.push_back(3);
		power_options.push_back(4);
		power_options.push_back(5);

		ofstream power_options_ofs("power_options.txt");

		bool first(true);
		for(power_options_t::iterator power_option = power_options.begin(); power_option != power_options.end(); power_option++) {
			if(first) {
				first = false;
			} else {
				power_options_ofs << " ";
			}

			power_options_ofs << *power_option;
		}

		power_options_ofs.close();

		power_options.push_back(6);
		power_options.push_back(7);
		power_options.push_back(8);
		power_options.push_back(9);
		power_options.push_back(10);
		power_options.push_back(11);
		power_options.push_back(12);
		power_options.push_back(13);
		power_options.push_back(14);
		power_options.push_back(15);
		power_options.push_back(103);
		power_options.push_back(104);
		power_options.push_back(105);
		power_options.push_back(106);
		power_options.push_back(107);
		power_options.push_back(108);
		power_options.push_back(109);
		power_options.push_back(110);
		power_options.push_back(111);
		power_options.push_back(112);
		power_options.push_back(113);
		power_options.push_back(114);
		power_options.push_back(115);

		ofstream power_options2_ofs("power_options.2.txt");

		first = true;
		for(power_options_t::iterator power_option = power_options.begin(); power_option != power_options.end(); power_option++) {
			if(first) {
				first = false;
			} else {
				power_options2_ofs << " ";
			}

			power_options2_ofs << *power_option;
		}

		power_options2_ofs.close();

		cout << endl << "power_options.txt file not found, example files saved" << endl << endl;
		exit(1);
	}

	ifstream power_options_ifs("power_options.txt");

	vector<string> tokens{istream_iterator<string>{power_options_ifs}, istream_iterator<string>{}};

	power_options_ifs.close();

	for(vector<string>::iterator itr = tokens.begin(); itr != tokens.end(); itr++) {
		try {
			int i = stoi(*itr);
			power_options.push_back(i);
		} catch(...) {
			cout << endl << "invalid power_options.txt format. it should be space seperated integers" << endl << endl;
			exit(1);
		}
	}

	cout << endl;

	xmr_stak_config_t xmr_stak_config(power_options, skip_prefetch);

	cout << endl;
	cout << "  number of cores: " << xmr_stak_config.get_number_of_cores() << endl;
	cout << " threads_per_core: " << xmr_stak_config.get_threads_per_core() << endl;
	cout << "        test time: " << xmr_stak_config.get_delay() << " seconds" << endl;
	cout << "    power options: " << xmr_stak_config.get_number_of_power_options() << endl;
	cout << " prefetch options: " << (skip_prefetch ? 1 : 2) << endl;
	cout << "   configurations: " << xmr_stak_config.get_number_of_configs() << endl;
	cout << "estimated runtime: " << xmr_stak_config.get_estimated_runtime() << endl;
	cout << endl;

	while(1) {
		cout << "proceed: [yN]? ";
		cin >> in;

		if(in == "Y" || in == "y") {
			break;
		}

		if(in == "N" || in == "n") {
			cout << endl;
			xmr_stak_config.finished();
			cout << endl;
			exit(1);
		}
	}

	cout << endl;

	ofstream output(LOG_FILE, ios::app);

	output << endl;
	output << "  number of cores: " << xmr_stak_config.get_number_of_cores() << endl;
	output << " threads_per_core: " << xmr_stak_config.get_threads_per_core() << endl;
	output << "        test time: " << xmr_stak_config.get_delay() << " seconds" << endl;
	output << "    power options: " << xmr_stak_config.get_number_of_power_options() << endl;
	output << " prefetch options: " << (skip_prefetch ? 1 : 2) << endl;
	output << "   configurations: " << xmr_stak_config.get_number_of_configs() << endl;
	output << "estimated runtime: " << xmr_stak_config.get_estimated_runtime() << endl;
	output << endl;

	output.close();

	xmr_stak_config.run();
}

