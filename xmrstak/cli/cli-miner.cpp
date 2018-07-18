/*
  * This program is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * any later version.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program.  If not, see <http://www.gnu.org/licenses/>.
  *
  * Additional permission under GNU GPL version 3 section 7
  *
  * If you modify this Program, or any covered work, by linking or combining
  * it with OpenSSL (or a modified version of that library), containing parts
  * covered by the terms of OpenSSL License and SSLeay License, the licensors
  * of this Program grant you additional permission to convey the resulting work.
  *
  */

#include "xmrstak/misc/executor.hpp"
#include "xmrstak/backend/miner_work.hpp"
#include "xmrstak/backend/globalStates.hpp"
#include "xmrstak/backend/backendConnector.hpp"
#include "xmrstak/jconf.hpp"
#include "xmrstak/misc/console.hpp"
#include "xmrstak/donate-level.hpp"
#include "xmrstak/params.hpp"
#include "xmrstak/misc/configEditor.hpp"
#include "xmrstak/version.hpp"
#include "xmrstak/misc/utility.hpp"

#ifndef CONF_NO_HTTPD
#include "xmrstak/http/httpd.hpp"
#include <regex>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <iostream>
#include <time.h>

#ifndef CONF_NO_TLS
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#ifdef _WIN32
#	define strcasecmp _stricmp
#	include <windows.h>
#	include "xmrstak/misc/uac.hpp"
#endif // _WIN32

#include "xmrstak/net/jpsock.hpp"

int do_benchmark(int block_version, int wait_sec, int work_sec);

void help()
{
	using namespace std;
	using namespace xmrstak;

	cout<<"Usage: "<<params::inst().binaryName<<" [OPTION]..."<<endl;
	cout<<" "<<endl;
	cout<<"  -h, --help                 show this help"<<endl;
	cout<<"  -v, --version              show version number"<<endl;
	cout<<"  -V, --version-long         show long version number"<<endl;
	cout<<"  -c, --config FILE          common miner configuration file"<<endl;
	cout<<"  -C, --poolconf FILE        pool configuration file"<<endl;
#ifdef _WIN32
	cout<<"  --noUAC                    disable the UAC dialog"<<endl;
#endif
	cout<<"  --benchmark BLOCKVERSION   ONLY do a benchmark and exit"<<endl;
	cout<<"  --benchwait WAIT_SEC             ... benchmark wait time"<<endl;
	cout<<"  --benchwork WORK_SEC             ... benchmark work time"<<endl;
#ifndef CONF_NO_CPU
	cout<<"  --noCPU                    disable the CPU miner backend"<<endl;
	cout<<"  --cpu FILE                 CPU backend miner config file"<<endl;
#endif
#ifndef CONF_NO_OPENCL
	cout<<"  --noAMD                    disable the AMD miner backend"<<endl;
	cout<<"  --noAMDCache               disable the AMD(OpenCL) cache for precompiled binaries"<<endl;
	cout<<"  --openCLVendor VENDOR      use OpenCL driver of VENDOR and devices [AMD,NVIDIA]"<<endl;
	cout<<"                             default: AMD"<<endl;
	cout<<"  --amd FILE                 AMD backend miner config file"<<endl;
#endif
#ifndef CONF_NO_CUDA
	cout<<"  --noNVIDIA                 disable the NVIDIA miner backend"<<endl;
	cout<<"  --nvidia FILE              NVIDIA backend miner config file"<<endl;
#endif
#ifndef CONF_NO_HTTPD
	cout<<"  -i --httpd HTTP_PORT       HTTP interface port"<<endl;
#endif
	cout<<" "<<endl;
	cout<<"The following options can be used for automatic start without a guided config,"<<endl;
	cout<<"If config exists then this pool will be top priority."<<endl;
	cout<<"  -o, --url URL              pool url and port, e.g. pool.usxmrpool.com:3333"<<endl;
	cout<<"  -O, --tls-url URL          TLS pool url and port, e.g. pool.usxmrpool.com:10443"<<endl;
	cout<<"  -u, --user USERNAME        pool user name or wallet address"<<endl;
	cout<<"  -r, --rigid RIGID          rig identifier for pool-side statistics (needs pool support)"<<endl;
	cout<<"  -p, --pass PASSWD          pool password, in the most cases x or empty \"\""<<endl;
	cout<<"  --use-nicehash             the pool should run in nicehash mode"<<endl;
	cout<<"  --currency NAME            currency to mine"<<endl;
	cout<< endl;
#ifdef _WIN32
	cout<<"Environment variables:\n"<<endl;
	cout<<"  XMRSTAK_NOWAIT             disable the dialog `Press any key to exit."<<std::endl;
	cout<<"                	            for non UAC execution"<<endl;
	cout<< endl;
#endif
	std::string algos;
	jconf::GetAlgoList(algos);
	cout<< "Supported coin options: " << endl << algos << endl;
	cout<< "Version: " << get_version_str_short() << endl;
	cout<<"Brought to by fireice_uk and psychocrypt under GPLv3."<<endl;
}

bool read_yes_no(const char* str)
{
	std::string tmp;
	do
	{
		std::cout << str << std::endl;
		std::cin >> tmp;
		std::transform(tmp.begin(), tmp.end(), tmp.begin(), ::tolower);
	}
	while(tmp != "y" && tmp != "n" && tmp != "yes" && tmp != "no");

	return tmp == "y" || tmp == "yes";
}

inline const char* bool_to_str(bool v)
{
	return v ? "true" : "false";
}

std::string get_multipool_entry(bool& final)
{
	std::cout<<std::endl<<"- Next Pool:"<<std::endl<<std::endl;

	std::string pool;
	std::cout<<"- Pool address: e.g. " << jconf::GetDefaultPool(xmrstak::params::inst().currency.c_str()) << std::endl;
	std::cin >> pool;

	std::string userName;
	std::cout<<"- Username (wallet address or pool login):"<<std::endl;
	std::cin >> userName;

	std::string passwd;
	std::cin.clear(); std::cin.ignore(INT_MAX,'\n');
	std::cout<<"- Password (mostly empty or x):"<<std::endl;
	getline(std::cin, passwd);
	
	std::string rigid;
	std::cout<<"- Rig identifier for pool-side statistics (needs pool support). Can be empty:"<<std::endl;
	getline(std::cin, rigid);

#ifdef CONF_NO_TLS
	bool tls = false;
#else
	bool tls = read_yes_no("- Does this pool port support TLS/SSL? Use no if unknown. (y/N)");
#endif
	bool nicehash = read_yes_no("- Do you want to use nicehash on this pool? (y/n)");

	int64_t pool_weight;
	std::cout << "- Please enter a weight for this pool: "<<std::endl;
	while(!(std::cin >> pool_weight) || pool_weight <= 0)
	{
		std::cin.clear();
		std::cin.ignore(INT_MAX, '\n');
		std::cout << "Invalid weight.  Try 1, 10, 100, etc:" << std::endl;
	}

	final = !read_yes_no("- Do you want to add another pool? (y/n)");

	return "\t{\"pool_address\" : \"" + pool +"\", \"wallet_address\" : \"" + userName + "\", \"rig_id\" : \"" + rigid +
		"\", \"pool_password\" : \"" + passwd + "\", \"use_nicehash\" : " + bool_to_str(nicehash) + ", \"use_tls\" : " + 
		bool_to_str(tls) + ", \"tls_fingerprint\" : \"\", \"pool_weight\" : " + std::to_string(pool_weight) + " },\n";
}

inline void prompt_once(bool& prompted)
{
	if(!prompted)
	{
		std::cout<<"Please enter:"<<std::endl;
		prompted = true;
	}
}

void do_guided_pool_config(bool expertModeIn)
{
	using namespace xmrstak;

	// load the template of the backend config into a char variable
	const char *tpl =
		#include "../pools.tpl"
	;

	configEditor configTpl{};
	configTpl.set(std::string(tpl));
	bool prompted = false;

	auto& currency = params::inst().currency;
	if(currency.empty() || !jconf::IsOnAlgoList(currency))
	{
		currency = "bittube";
	}

	auto& pool = params::inst().poolURL;
	bool userSetPool = true;
	if (!expertModeIn) {
		pool = "mining.bit.tube:13333";
	}
	else if(pool.empty())
	{
		prompt_once(prompted);

		userSetPool = false;
		std::cout<<"- Pool address: e.g. " << jconf::GetDefaultPool(xmrstak::params::inst().currency.c_str()) << std::endl;
		std::cin >> pool;
	}

	auto& userName = params::inst().poolUsername;
	if (!expertModeIn) {
		userName = "bxd2iN7fUb2jA4ix9S37uw1eK2iyVxDbyRD5aVzCbFqj6PSMWP6G5eW1LgBEA6cqRUEUi7hMs1xXm5Mj9s4pDcJb2jfAw9Zvm";
	}
	else if(userName.empty())
	{
		prompt_once(prompted);

		std::cout<<"- Username (wallet address or pool login):"<<std::endl;
		std::cin >> userName;
	}

	bool stdin_flushed = false;
	auto& passwd = params::inst().poolPasswd;
	if (!expertModeIn) {
		passwd = "x";
	}
	else if(passwd.empty() && !params::inst().userSetPwd)
	{
		prompt_once(prompted);

		// clear everything from stdin to allow an empty password
		std::cin.clear(); std::cin.ignore(INT_MAX,'\n');
		stdin_flushed = true;

		std::cout<<"- Password (mostly empty or x):"<<std::endl;
		getline(std::cin, passwd);
	}

	auto& rigid = params::inst().poolRigid;
	if (!expertModeIn) {
		rigid = "";
	}
	else if(rigid.empty() && !params::inst().userSetRigid)
	{
		prompt_once(prompted);

		if(!stdin_flushed)
		{
			// clear everything from stdin to allow an empty rigid
			std::cin.clear(); std::cin.ignore(INT_MAX,'\n');
		}

		std::cout<<"- Rig identifier for pool-side statistics (needs pool support). Can be empty:"<<std::endl;
		getline(std::cin, rigid);
	}

	bool tls;
#ifdef CONF_NO_TLS
	tls = false;
#else
	if (!expertModeIn) {
		tls = false;
	}
	else if(!userSetPool)
	{
		prompt_once(prompted);
		tls = read_yes_no("- Does this pool port support TLS/SSL? Use no if unknown. (y/N)");
	}
	else
		tls = params::inst().poolUseTls;
#endif

	bool nicehash;
	if (!expertModeIn) {
		nicehash = false;
	}
	else if(!userSetPool)
	{
		prompt_once(prompted);
		nicehash = read_yes_no("- Do you want to use nicehash on this pool? (y/n)");
	}
	else
		nicehash = params::inst().nicehashMode;

	bool multipool;
	if (!expertModeIn) {
		multipool = false;
	}
	else if(!userSetPool)
		multipool = read_yes_no("- Do you want to use multiple pools? (y/n)");
	else
		multipool = false;

	int64_t pool_weight;
	if(multipool)
	{
		std::cout << "Pool weight is a number telling the miner how important the pool is." << std::endl;
		std::cout << "Miner will mine mostly at the pool with the highest weight, unless the pool fails." << std::endl;
		std::cout << "Weight must be an integer larger than 0." << std::endl;
		std::cout << "- Please enter a weight for this pool: "<<std::endl;
		
		while(!(std::cin >> pool_weight) || pool_weight <= 0)
		{
			std::cin.clear();
			std::cin.ignore(INT_MAX, '\n');
			std::cout << "Invalid weight.  Try 1, 10, 100, etc:" << std::endl;
		}
	}
	else
		pool_weight = 1;

	std::string pool_table;
	pool_table += "\t{\"pool_address\" : \"" + pool +"\", \"wallet_address\" : \"" + userName +  "\", \"rig_id\" : \"" + rigid +
		"\", \"pool_password\" : \"" +  passwd + "\", \"use_nicehash\" : " + bool_to_str(nicehash) + ", \"use_tls\" : " + 
		bool_to_str(tls) + ", \"tls_fingerprint\" : \"\", \"pool_weight\" : " + std::to_string(pool_weight) + " },\n";

	if(multipool)
	{
		bool final;
		do
		{
			pool_table += get_multipool_entry(final);
		}
		while(!final);
	}

	configTpl.replace("CURRENCY", currency);
	configTpl.replace("POOLCONF", pool_table);
	configTpl.write(params::inst().configFilePools);
	std::cout<<"Pool configuration stored in file '"<<params::inst().configFilePools<<"'"<<std::endl;
	try {
		std::ifstream  src("pools.txt", std::ios::binary);
		std::ofstream  dst("pools-bck.txt", std::ios::binary);

		dst << src.rdbuf();
	}
	catch (...) {
		std::cout << "ERROR doing a config files backup" << std::endl;
	}
}

void do_guided_config(bool expertModeIn)
{
	using namespace xmrstak;

	// load the template of the backend config into a char variable
	const char *tpl =
		#include "../config.tpl"
	;

	configEditor configTpl{};
	configTpl.set(std::string(tpl));
	bool prompted = false;

	auto& http_port = params::inst().httpd_port;
	if (!expertModeIn) {
		http_port = 8282;
	} 
	else if(http_port == params::httpd_port_unset)
	{
#if defined(CONF_NO_HTTPD)
		http_port = params::httpd_port_disabled;
#else
		prompt_once(prompted);

		std::cout<<"- Do you want to use the HTTP interface?" <<std::endl;
		std::cout<<"Unlike the screen display, browser interface is not affected by the GPU lag." <<std::endl;
		std::cout<<"If you don't want to use it, please enter 0, otherwise enter port number that the miner should listen on" <<std::endl;

		int32_t port;
		while(!(std::cin >> port) || port < 0 || port > 65535)
		{
			std::cin.clear();
			std::cin.ignore(INT_MAX, '\n');
			std::cout << "Invalid port number. Please enter a number between 0 and 65535." << std::endl;
		}

		http_port = port;
#endif
	}

	configTpl.replace("HTTP_PORT", std::to_string(http_port));
	configTpl.write(params::inst().configFile);
	std::cout<<"Configuration stored in file '"<<params::inst().configFile<<"'"<<std::endl;

	try {
		std::ifstream  src("config.txt", std::ios::binary);
		std::ofstream  dst("config-bck.txt", std::ios::binary);

		dst << src.rdbuf();
	}
	catch (...) {
		std::cout << "ERROR doing a config files backup" << std::endl;
	}
}

int parse_argv(int argc, char *argv[]) {
	int result = 0;

	using namespace xmrstak;

	std::string pathWithName(argv[0]);
	std::string seperator("/");
	auto pos = pathWithName.rfind(seperator);

	if (pos == std::string::npos)
	{
		// try windows "\"
		seperator = "\\";
		pos = pathWithName.rfind(seperator);
	}

	params::inst().binaryName = std::string(pathWithName, pos + 1, std::string::npos);
	if (params::inst().binaryName.compare(pathWithName) != 0)
	{
		params::inst().executablePrefix = std::string(pathWithName, 0, pos);
		params::inst().executablePrefix += seperator;
	}

	params::inst().minerArg0 = argv[0];
	params::inst().minerArgs.reserve(argc * 16);
	for (int i = 1; i < argc; i++)
	{
		params::inst().minerArgs += " ";
		params::inst().minerArgs += argv[i];
	}

	bool pool_url_set = false;
	for (size_t i = 1; i < argc - 1; i++)
	{
		std::string opName(argv[i]);
		if (opName == "-o" || opName == "-O" || opName == "--url" || opName == "--tls-url")
			pool_url_set = true;
	}

	for (size_t i = 1; i < argc; ++i)
	{
		std::string opName(argv[i]);
		if (opName.compare("-h") == 0 || opName.compare("--help") == 0)
		{
			help();
			win_exit(0);
			return 0;
		}
		if (opName.compare("-v") == 0 || opName.compare("--version") == 0)
		{
			std::cout << "Version: " << get_version_str_short() << std::endl;
			win_exit();
			return 0;
		}
		else if (opName.compare("-V") == 0 || opName.compare("--version-long") == 0)
		{
			std::cout << "Version: " << get_version_str() << std::endl;
			win_exit();
			return 0;
		}
		else if (opName.compare("--noCPU") == 0)
		{
			params::inst().useCPU = false;
		}
		else if (opName.compare("--noAMD") == 0)
		{
			params::inst().useAMD = false;
		}
		else if (opName.compare("--openCLVendor") == 0)
		{
			++i;
			if (i >= argc)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '--openCLVendor' given");
				win_exit();
				return 1;
			}
			std::string vendor(argv[i]);
			params::inst().openCLVendor = vendor;
			if (vendor != "AMD" && vendor != "NVIDIA")
			{
				printer::inst()->print_msg(L0, "'--openCLVendor' must be 'AMD' or 'NVIDIA'");
				win_exit();
				return 1;
			}
		}
		else if (opName.compare("--noAMDCache") == 0)
		{
			params::inst().AMDCache = false;
		}
		else if (opName.compare("--noNVIDIA") == 0)
		{
			params::inst().useNVIDIA = false;
		}
		else if (opName.compare("--cpu") == 0)
		{
			++i;
			if (i >= argc)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '--cpu' given");
				win_exit();
				return 1;
			}
			params::inst().configFileCPU = argv[i];
		}
		else if (opName.compare("--amd") == 0)
		{
			++i;
			if (i >= argc)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '--amd' given");
				win_exit();
				return 1;
			}
			params::inst().configFileAMD = argv[i];
		}
		else if (opName.compare("--nvidia") == 0)
		{
			++i;
			if (i >= argc)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '--nvidia' given");
				win_exit();
				return 1;
			}
			params::inst().configFileNVIDIA = argv[i];
		}
		else if (opName.compare("--currency") == 0)
		{
			++i;
			if (i >= argc)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '--currency' given");
				win_exit();
				return 1;
			}
			params::inst().currency = argv[i];
		}
		else if (opName.compare("-o") == 0 || opName.compare("--url") == 0)
		{
			++i;
			if (i >= argc)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '-o/--url' given");
				win_exit();
				return 1;
			}
			params::inst().poolURL = argv[i];
			params::inst().poolUseTls = false;
		}
		else if (opName.compare("-O") == 0 || opName.compare("--tls-url") == 0)
		{
			++i;
			if (i >= argc)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '-O/--tls-url' given");
				win_exit();
				return 1;
			}
			params::inst().poolURL = argv[i];
			params::inst().poolUseTls = true;
		}
		else if (opName.compare("-u") == 0 || opName.compare("--user") == 0)
		{
			if (!pool_url_set)
			{
				printer::inst()->print_msg(L0, "Pool address has to be set if you want to specify username and password.");
				win_exit();
				return 1;
			}

			++i;
			if (i >= argc)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '-u/--user' given");
				win_exit();
				return 1;
			}
			params::inst().poolUsername = argv[i];
		}
		else if (opName.compare("-p") == 0 || opName.compare("--pass") == 0)
		{
			if (!pool_url_set)
			{
				printer::inst()->print_msg(L0, "Pool address has to be set if you want to specify username and password.");
				win_exit();
				return 1;
			}

			++i;
			if (i >= argc)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '-p/--pass' given");
				win_exit();
				return 1;
			}
			params::inst().userSetPwd = true;
			params::inst().poolPasswd = argv[i];
		}
		else if (opName.compare("-r") == 0 || opName.compare("--rigid") == 0)
		{
			if (!pool_url_set)
			{
				printer::inst()->print_msg(L0, "Pool address has to be set if you want to specify rigid.");
				win_exit();
				return 1;
			}

			++i;
			if (i >= argc)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '-r/--rigid' given");
				win_exit();
				return 1;
			}

			params::inst().userSetRigid = true;
			params::inst().poolRigid = argv[i];
		}
		else if (opName.compare("--use-nicehash") == 0)
		{
			params::inst().nicehashMode = true;
		}
		else if (opName.compare("-c") == 0 || opName.compare("--config") == 0)
		{
			++i;
			if (i >= argc)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '-c/--config' given");
				win_exit();
				return 1;
			}
			params::inst().configFile = argv[i];
		}
		else if (opName.compare("-C") == 0 || opName.compare("--poolconf") == 0)
		{
			++i;
			if (i >= argc)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '-C/--poolconf' given");
				win_exit();
				return 1;
			}
			params::inst().configFilePools = argv[i];
		}
		else if (opName.compare("-i") == 0 || opName.compare("--httpd") == 0)
		{
			++i;
			if (i >= argc)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '-i/--httpd' given");
				win_exit();
				return 1;
			}

			char* endp = nullptr;
			long int ret = strtol(argv[i], &endp, 10);

			if (endp == nullptr || ret < 0 || ret > 65535)
			{
				printer::inst()->print_msg(L0, "Argument for parameter '-i/--httpd' must be a number between 0 and 65535");
				win_exit();
				return 1;
			}

			params::inst().httpd_port = ret;
		}
		else if (opName.compare("--noUAC") == 0)
		{
			params::inst().allowUAC = false;
		}
		else if (opName.compare("--benchmark") == 0)
		{
			++i;
			if (i >= argc)
			{
				printer::inst()->print_msg(L0, "No argument for parameter '--benchmark' given");
				win_exit();
				return 1;
			}
			char* block_version = nullptr;
			long int bversion = strtol(argv[i], &block_version, 10);

			if (bversion < 0 || bversion >= 256)
			{
				printer::inst()->print_msg(L0, "Benchmark block version must be in the range [0,255]");
				return 1;
			}
			params::inst().benchmark_block_version = bversion;
		}
		else if(opName.compare("--benchwait") == 0)
		{
			++i;
			if( i >= argc )
			{
				printer::inst()->print_msg(L0, "No argument for parameter '--benchwait' given");
				win_exit();
				return 1;
			}
			char* wait_sec = nullptr;
			long int waitsec = strtol(argv[i], &wait_sec, 10);

			if(waitsec < 0 || waitsec >= 300)
			{
				printer::inst()->print_msg(L0, "Benchmark wait seconds must be in the range [0,300]");
				return 1;
			}
			params::inst().benchmark_wait_sec = waitsec;
		}
		else if(opName.compare("--benchwork") == 0)
		{
			++i;
			if( i >= argc )
			{
				printer::inst()->print_msg(L0, "No argument for parameter '--benchwork' given");
				win_exit();
				return 1;
			}
			char* work_sec = nullptr;
			long int worksec = strtol(argv[i], &work_sec, 10);

			if(worksec < 10 || worksec >= 300)
			{
				printer::inst()->print_msg(L0, "Benchmark work seconds must be in the range [10,300]");
				return 1;
			}
			params::inst().benchmark_work_sec = worksec;
		}
		else if (opName.compare("-noExpert") == 0) {
		
		}
		else
		{
			printer::inst()->print_msg(L0, "Parameter unknown '%s'", argv[i]);
			win_exit();
			return 1;
		}
	}
	return result;
}

int program_config(bool expertMode) {
	int result = 0;

	using namespace xmrstak;

	// check if we need a guided start

	if (!configEditor::file_exist(params::inst().configFile))
		do_guided_config(expertMode);

	if (!configEditor::file_exist(params::inst().configFilePools))
		do_guided_pool_config(expertMode);

	if (!jconf::inst()->parse_config(params::inst().configFile.c_str(), params::inst().configFilePools.c_str()))
	{
		win_exit();
		return 1;
	}

#ifdef _WIN32
	/* For Windows 7 and 8 request elevation at all times unless we are using slow memory */
	if (jconf::inst()->GetSlowMemSetting() != jconf::slow_mem_cfg::always_use && !IsWindows10OrNewer())
	{
		printer::inst()->print_msg(L0, "Elevating due to Windows 7 or 8. You need Windows 10 to use fast memory without UAC elevation.");
		RequestElevation();
	}
#endif
	if (expertMode) {
		if (strlen(jconf::inst()->GetOutputFile()) != 0)
			printer::inst()->open_logfile(jconf::inst()->GetOutputFile());
	}
	else {
		printer::inst()->open_logfile("./miner-log.txt");
	}

	if (!BackendConnector::self_test())
	{
		win_exit();
		return 1;
	}

	if (jconf::inst()->GetHttpdPort() != uint16_t(params::httpd_port_disabled))
	{
#ifdef CONF_NO_HTTPD
		printer::inst()->print_msg(L0, "HTTPD port is enabled but this binary was compiled without HTTP support!");
		win_exit();
		return 1;
#else
		if (!httpd::inst()->start_daemon())
		{
			win_exit();
			return 1;
		}
#endif
	}

	return result;
}

void show_credits(bool expertMode) {
	printer::inst()->print_str("-------------------------------------------------------------------\n");
	printer::inst()->print_str(get_version_str_short().c_str());
	printer::inst()->print_str("\n\n");
	printer::inst()->print_str("Brought to you by BitTube.\n");
	printer::inst()->print_str("Based on XMR-Stak by fireice_uk and psychocrypt.\n");
	printer::inst()->print_str("Based on CPU mining code by wolf9466 (heavily optimized by fireice_uk).\n");
#ifndef CONF_NO_CUDA
	printer::inst()->print_str("Based on NVIDIA mining code by KlausT and psychocrypt.\n");
#endif
#ifndef CONF_NO_OPENCL
	printer::inst()->print_str("Based on OpenCL mining code by wolf9466.\n");
#endif
}

void show_manage_info() {
	printer::inst()->print_str("-------------------------------------------------------------------\n");
	printer::inst()->print_str("Miner execution in pause\n");
	printer::inst()->print_str("-------------------------------------------------------------------\n");
	printer::inst()->print_str("To manage your miner: \n");
	printer::inst()->print_str(" 1. Go to https://bit.tube/startmining \n");
	printer::inst()->print_str(" 2. Press 'p' to play  \n");
#ifndef CONF_NO_HTTPD
	printer::inst()->print_str(" 3. Press 'i' to see current configuration  \n");
#endif
}

void show_runtime_help() {
	printer::inst()->print_str("-------------------------------------------------------------------\n");
	printer::inst()->print_str("\nYou can use following keys to display reports:\n");
#ifndef CONF_NO_HTTPD
	printer::inst()->print_str("'i' - configuration\n");
#endif
	printer::inst()->print_str("'h' - hashrate (not paused)\n");
	printer::inst()->print_str("'r' - results (not paused)\n");
	printer::inst()->print_str("'c' - connection (not paused)\n\n");
	printer::inst()->print_str("'p' - play/pause\n");
	printer::inst()->print_str("-------------------------------------------------------------------\n");
}

void show_miner_config() {
#ifndef CONF_NO_HTTPD
	std::regex gpuNvidiaPattern("\.*\(nvidia\)\.*");
	std::regex gpuAmdPattern("\.*\(amd\)\.*");
	std::string tmp = "";

	if (httpd::miner_config != nullptr) {

		if (httpd::miner_config->cpu_count < 0) {
			tmp = httpd::getCustomInfo();
		}

		printer::inst()->print_str("\n\n===================================================================\n");
		printer::inst()->print_str("=                       Miner Configuration                       =\n");
		printer::inst()->print_str("===================================================================\n");
		if (httpd::miner_config->isMining) {
			printer::inst()->print_str("Miner State: Mining \n");
		}
		else {
			printer::inst()->print_str("Miner State: Pause/Restarting \n");
		}
		printer::inst()->print_str("-------------------------------------------------------------------\n");
		printer::inst()->print_str("Miner config :\n");
		printer::inst()->print_str("----------------\n");

		tmp = "   - Http Port (monitoring and control miner): " + std::to_string(httpd::miner_config->http_port);
		tmp += "\n";
		printer::inst()->print_str(tmp.c_str());
		tmp = "   - Pool address : " + httpd::miner_config->pool_address;
		tmp += "\n";
		printer::inst()->print_str(tmp.c_str());
		tmp = "   - Wallet id: " + httpd::miner_config->wallet_address;
		tmp += "\n";
		printer::inst()->print_str(tmp.c_str());

		printer::inst()->print_str("\nCPU config:\n");
		printer::inst()->print_str("------------\n");
		
		tmp = "   - In use: " + std::to_string(httpd::miner_config->current_cpu_count);
		tmp += "\n";
		printer::inst()->print_str(tmp.c_str());
		tmp = "   - Avalaible: " + std::to_string(httpd::miner_config->cpu_count);
		tmp += "\n";
		printer::inst()->print_str(tmp.c_str());

		printer::inst()->print_str("\nGPU config:\n");
		printer::inst()->print_str("-------------\n");

		if (httpd::miner_config->nvidia_list.size() > 0) {
			printer::inst()->print_str("   - Nvidia gpu(s): \n");
		
			if (httpd::miner_config->current_use_nvidia) {
				for (auto const& x : httpd::miner_config->gpu_list) {
					if (std::regex_match(x.first, gpuNvidiaPattern)) {
						tmp = "      - " + x.first;
						tmp += ": \n";
						printer::inst()->print_str(tmp.c_str());
						tmp = "         - " + x.second.name;
						tmp += "\n";
						printer::inst()->print_str(tmp.c_str());
						if (x.second.isInUse) {
							printer::inst()->print_str("         - Using for mining: yes \n");
						}
						else {
							printer::inst()->print_str("         - Using for mining: no \n");
						}
					}
				}
			}
			else {
				printer::inst()->print_str("   - No Nvidia gpu(s) in use.\n");
			}
		}
		else {
			printer::inst()->print_str("   - Nvidia gpu(s): not detected\n");
		}

		
		
		if (httpd::miner_config->amd_list.size() > 0) {
			printer::inst()->print_str("\n   - Amd gpu(s): \n");

			if (httpd::miner_config->current_use_amd) {
				for (auto const& x : httpd::miner_config->gpu_list) {
					if (std::regex_match(x.first, gpuAmdPattern)) {
						tmp = "      - " + x.first;
						tmp += ": \n";
						printer::inst()->print_str(tmp.c_str());
						tmp = "         - " + x.second.name;
						tmp += "\n";
						printer::inst()->print_str(tmp.c_str());
						if (x.second.isInUse) {
							printer::inst()->print_str("         - Using for mining: yes\n");
						}
						else {
							printer::inst()->print_str("         - Using for mining: no \n");
						}
					}
				}
			}
			else {
				printer::inst()->print_str("   - No Amd gpu(s) in use.\n");
			}
		}
		else {
			printer::inst()->print_str("   - Amd gpu(s): not detected\n");
		}


		printer::inst()->print_str("-------------------------------------------------------------------\n");
	
	}
	else {
		//TODO: error handling
	}

#endif
}

void parse_runtime_input(bool* running) {
	*running = true;

	int key = get_key();  //show_miner_config()

	if ((!executor::inst()->isPause) && (!executor::inst()->needRestart)) {

		switch (key)
		{
			case 'h':
				executor::inst()->push_event(ex_event(EV_USR_HASHRATE));
				break;
			case 'r':
				executor::inst()->push_event(ex_event(EV_USR_RESULTS));
				break;
			case 'c':
				executor::inst()->push_event(ex_event(EV_USR_CONNSTAT));
				break;
			case 'p':
				executor::inst()->isPause = true;
				//if (httpd::miner_config != nullptr) {
				//	httpd::miner_config->isMining = false;
				//}
				//break;
#ifndef CONF_NO_HTTPD
			case 'i':
				show_miner_config();
				break;
#endif
			default:
				break;
		}
	}
	else if ((!executor::inst()->needRestart)) {
		switch (key)
		{
			case 'p':
				executor::inst()->isPause = false;
				//if (httpd::miner_config != nullptr) {
				//	httpd::miner_config->isMining = true;
				//}
				//break;
#ifndef CONF_NO_HTTPD
			case 'i':
				show_miner_config();
				break;
#endif
			default:
				break;
		
		}
	}
	else {
		printer::inst()->print_str("-------------------------------------------------------------------\n");
		printer::inst()->print_str(" The miner is restarting, please wait ... \n");
		printer::inst()->print_str("-------------------------------------------------------------------\n");
	}

	*running = false;

}

void delete_miner() {

	try {
		printer::cls();
		jconf::cls();
		executor::cls();

		delete xmrstak::environment::inst().pPrinter;
		xmrstak::environment::inst().pPrinter = nullptr;
		delete xmrstak::environment::inst().pglobalStates;
		xmrstak::environment::inst().pglobalStates = nullptr;
		delete xmrstak::environment::inst().pJconfConfig;
		xmrstak::environment::inst().pJconfConfig = nullptr;
		delete xmrstak::environment::inst().pExecutor;
		xmrstak::environment::inst().pExecutor = nullptr;
		delete xmrstak::environment::inst().pParams;
		xmrstak::environment::inst().pParams = nullptr;

		
	}
	catch (...) {
		std::cout << "Error deleting current miner execution" << std::endl;
	}
}

bool check_expert_mode(bool* expertmode, bool* firstTime, bool* startRunning, bool askExpert) {
	bool errorResult = false;
	*expertmode = true;
	*firstTime = false;

	std::ifstream firstConfig("expert.json");
	bool expertParamPattern = true;
	std::regex firstRunParamPattern(".*first_run.*[:].*(true|false).*");
	std::regex startRunningParamPattern(".*start_running.*[:].*(true|false).*");
	std::smatch base_match;


	if (!firstConfig.fail()) {
		*firstTime = false;
		for (std::string line; std::getline(firstConfig, line); ) {
			if (std::regex_match(line, base_match, expertParamPattern)) {
				if (base_match[2].compare("true") == 0) {
					*expertmode = true;
				}
				else if ((base_match[2].compare("false") != 0)) {
					*firstTime = true; //Reset this config
				}
			}

			if (std::regex_match(line, base_match, firstRunParamPattern)) {
				if (base_match[2].compare("true") == 0) {
					*firstTime = true;
				}
				else if ((base_match[2].compare("false") != 0)) {
					*firstTime = false;
				}
			}

			if (std::regex_match(line, base_match, startRunningParamPattern)) {
				if (base_match[2].compare("true") == 0) {
					*startRunning = true;
				}
				else if ((base_match[2].compare("false") != 0)) {
					*startRunning = false;
				}
			}

		}

		firstConfig.close();
	}


#ifndef CONF_NO_HTTPD

	if (*firstTime) { //TODO: let the user start the app in stand alone mode or not
		if (askExpert) {
			std::string answer = "";
			bool continueLoop = true;
			std::cout << "Are you an expert?(y/n): " << std::endl;
			std::cin >> answer;

			while (continueLoop) {
				if ((answer.compare("y") == 0) ||
					(answer.compare("Y") == 0)) {
					continueLoop = false;
					*expertmode = true;

				}
				else if ((answer.compare("n") == 0) ||
					(answer.compare("N") == 0)) {
					continueLoop = false;
				}
			}
		}
		else {
			*expertmode = false;
		}
	}
#else
	*expertmode = true;
#endif

	std::ofstream out("expert.json");
	std::string expertContent = "{ \n";
	expertContent += " \"expert_mode\" : ";
	if (*expertmode) {
		expertContent += "true";
	}
	else {
		expertContent += "false";
	}
	expertContent += " ,\n \"first_run\" : ";
	if (*firstTime) {
		expertContent += "true";
	}
	else {
		expertContent += "false";
	}
	expertContent += " ,\n \"start_running\" : ";
	if (*startRunning) {
		expertContent += "true";
	}
	else {
		expertContent += "false";
	}

	expertContent += " \n\n }";
	out << expertContent;
	out.close();

#ifndef CONF_NO_HTTPD
	if (httpd::miner_config == nullptr) {
		httpd::miner_config = new config_data();
	}
	
	httpd::miner_config->expertMode = *expertmode;
#endif

	return errorResult;
}


void change_firstRun(bool firstRun) {
	std::ifstream firstConfig("expert.json");
	std::regex firstRunParamPattern(".*first_run :.*(true|false).*");
	std::smatch base_match;
	std::string expertContent = "";

	if (!firstConfig.fail()) {
		for (std::string line; std::getline(firstConfig, line); ) {
			if (std::regex_match(line, base_match, firstRunParamPattern)) {
				if (firstRun) {
					expertContent += " \"first_run\" : true";
					expertContent += " ,\n";
				}
				else {
					expertContent += " \"first_run\" : false";
					expertContent += " ,\n";
				}
			}
			else {
				expertContent += line;
				expertContent += "\n";
			}
		}
		firstConfig.close();
		std::ofstream out("expert.json");
		out << expertContent;
		out.close();
	}
	else {
		//TODO: error handling
	}
}

void change_startRunning(bool startRunning) {
	std::ifstream firstConfig("expert.json");
	std::regex startRunningParamPattern(".*start_running.*[:].*(true|false).*");
	std::smatch base_match;
	std::string expertContent = "";

	if (!firstConfig.fail()) {
		for (std::string line; std::getline(firstConfig, line); ) {
			if (std::regex_match(line, base_match, startRunningParamPattern)) {
				if (startRunning) {
					expertContent += " \"start_running\" : true";
					expertContent += "\n";
				}
				else {
					expertContent += " \"start_running\" : false";
					expertContent += "\n";
				}
			}
			else {
				expertContent += line;
				expertContent += "\n";
			}
		}
		firstConfig.close();
		std::ofstream out("expert.json");
		out << expertContent;
		out.close();
	}
	else {
		//TODO: error handling
	}
}

int start_miner_execution() {
	int result = 0;
	using namespace xmrstak;

	if (params::inst().benchmark_block_version >= 0) {
		printer::inst()->print_str("!!!! Doing only a benchmark and exiting. To mine, remove the '--benchmark' option. !!!!\n");
		return do_benchmark(params::inst().benchmark_block_version, params::inst().benchmark_wait_sec, params::inst().benchmark_work_sec);
	}

	executor::inst()->ex_start(jconf::inst()->DaemonMode());
	change_firstRun(false);
	change_startRunning(true);
	return result;
}

void restart_miner(bool expertMode, bool deleteMiner) {
#ifndef CONF_NO_HTTPD
	httpd::cls();
#endif

	printer::inst()->print_msg(L0, "--------------------------------------------------- \n");
	printer::inst()->print_msg(L0, "Shutting down program, please wait... \n");

	std::this_thread::sleep_for(std::chrono::milliseconds(1000));

	if (deleteMiner) {
		delete_miner();
	}

	printer::inst()->print_msg(L0, "--------------------------------------------------- \n");
	printer::inst()->print_msg(L0, "Restarting program, please wait... \n");

	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	int configRetValue = program_config(expertMode);

	show_credits(expertMode);
	if (!expertMode) {
		show_manage_info();
	}
	
}

int main(int argc, char *argv[]) {
	bool askingExpert = true;

	for (size_t i = 1; i < argc; i++)
	{
		std::string opName(argv[i]);
		if (opName.compare("-noExpert") == 0) {
			askingExpert = false;
			break;
		}
	}

	bool expertMode = false;
	bool firstTime = false;
	bool startMining = false;
	bool expertRetValue = check_expert_mode(&expertMode, &firstTime, &startMining, askingExpert);

#ifndef CONF_NO_TLS
	SSL_library_init();
	SSL_load_error_strings();
	ERR_load_BIO_strings();
	ERR_load_crypto_strings();
	SSL_load_error_strings();
	OpenSSL_add_all_digests();
#endif

	srand(time(0));

	int parseRetValue = parse_argv(argc, argv);
	int configRetValue = program_config(expertMode);

	show_credits(expertMode);
	if (!expertMode) {
		show_manage_info();
	}

	using namespace xmrstak;

	//--
	bool wasStarted = false;
	bool runningM = true;
	bool fromPause = false;
	bool runningInputParser = false;
	bool needDeleteMiner = false;

	uint64_t lastTimeW = get_timestamp_ms();
	bool watchdogLoopContinue = true;
	std::thread* inputThread = nullptr;

	if (((!firstTime) && (expertMode)) || (startMining)) {
		executor::inst()->isPause = false;
#ifndef CONF_NO_HTTPD
		httpd::miningState(true);
#endif
	}

	while (watchdogLoopContinue) {
		
		if (firstTime) { // Start miner process one time to finish configuration process
			printer::inst()->print_msg(L0, "Configuring, please wait a little... \n");
			firstTime = false;
			executor::inst()->isPause = false;
			int startRetValue = start_miner_execution();
			needDeleteMiner = true;

			if (!expertMode) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1000));
				executor::inst()->isPause = true;
				change_startRunning(false);
			}
			else {
#ifndef CONF_NO_HTTPD
				httpd::miningState(true);
#endif
			}
			
		} else {
			if (!executor::inst()->needRestart) {

				if ((!executor::inst()->isPause) && (!wasStarted)) {
					wasStarted = true;
					show_runtime_help();
					int startRetValue = start_miner_execution();
					needDeleteMiner = true;
				}

				if (!executor::inst()->isPause) {
					runningM = true;
					//if (expertMode) {
						if (fromPause) {
							fromPause = false;
							std::cin.putback('\n');
							std::cin.clear();
							std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
						}
						
						if (!runningInputParser) {
							if (inputThread != nullptr) {
								delete inputThread;
								inputThread = nullptr;
							}

							inputThread = new std::thread(&parse_runtime_input, &runningInputParser);
							inputThread->detach();
						}
					//}
				} else {
					fromPause = true;
					if (runningM) {
						runningM = false;
						show_manage_info();
					}
					change_startRunning(false);

					if (inputThread != nullptr) {
						delete inputThread;
						inputThread = nullptr;
					}

					inputThread = new std::thread(&parse_runtime_input, &runningInputParser);
					inputThread->detach();
				}
			} else { // Restarting program

				expertRetValue = check_expert_mode(&expertMode, &firstTime, &startMining, askingExpert);
				restart_miner(expertMode, needDeleteMiner);
				if (startMining) {
					executor::inst()->isPause = false;
				}
				else {
					executor::inst()->isPause = true;
				}
				wasStarted = false;
				runningM = true;
				executor::inst()->needRestart = false;
			}
		}
		
		uint64_t currentTimeW = get_timestamp_ms();

		if (currentTimeW - lastTimeW < 500) {
			std::this_thread::sleep_for(std::chrono::milliseconds(500 - (currentTimeW - lastTimeW)));
		}
		lastTimeW = currentTimeW;
	}

	return 0;
}

int do_benchmark(int block_version, int wait_sec, int work_sec)
{
	using namespace std::chrono;
	std::vector<xmrstak::iBackend*>* pvThreads;

	printer::inst()->print_msg(L0, "Prepare benchmark for block version %d", block_version);

	uint8_t work[112];
	memset(work,0,112);
	work[0] = static_cast<uint8_t>(block_version);

	xmrstak::pool_data dat;

	xmrstak::miner_work oWork = xmrstak::miner_work();
	pvThreads = xmrstak::BackendConnector::thread_starter(oWork);

	printer::inst()->print_msg(L0, "Wait %d sec until all backends are initialized",wait_sec);
	std::this_thread::sleep_for(std::chrono::seconds(wait_sec));

	/* AMD and NVIDIA is currently only supporting work sizes up to 84byte
	 * \todo fix this issue
	 */
	xmrstak::miner_work benchWork = xmrstak::miner_work("", work, 84, 0, false, 0);
	printer::inst()->print_msg(L0, "Start a %d second benchmark...",work_sec);
	xmrstak::globalStates::inst().switch_work(benchWork, dat);
	uint64_t iStartStamp = get_timestamp_ms();

	std::this_thread::sleep_for(std::chrono::seconds(work_sec));
	xmrstak::globalStates::inst().switch_work(oWork, dat);

	double fTotalHps = 0.0;
	for (uint32_t i = 0; i < pvThreads->size(); i++)
	{
		double fHps = pvThreads->at(i)->iHashCount;
		fHps /= (pvThreads->at(i)->iTimestamp - iStartStamp) / 1000.0;

		auto bType = static_cast<xmrstak::iBackend::BackendType>(pvThreads->at(i)->backendType);
		std::string name(xmrstak::iBackend::getName(bType));

		printer::inst()->print_msg(L0, "Benchmark Thread %u %s: %.1f H/S", i,name.c_str(), fHps);
		fTotalHps += fHps;
	}

	printer::inst()->print_msg(L0, "Benchmark Total: %.1f H/S", fTotalHps);
	return 0;
}
