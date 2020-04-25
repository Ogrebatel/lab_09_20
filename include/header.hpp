// Copyright 2018 Your Name <your_email>

#ifndef INCLUDE_HEADER_HPP_
#define INCLUDE_HEADER_HPP_

#include <iostream>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/program_options.hpp>
#include <gumbo.h>
#include <root_certificates.h>
#include <cstdlib>
#include <string>
#include <queue>
#include <thread>
#include <utility>
#include <vector>
#include <atomic>

using tcp = boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;
namespace http = boost::beast::http;
namespace po = boost::program_options;

using std::string;
using std::queue;
using std::vector;
using std::thread;
using std::cout;
using std::endl;
using std::mutex;
using std::atomic_uint;
using std::atomic_bool;

const char HTTPS[] = "https";
const char HTTP[] = "http";
const char HTTPS_NUM[] = "443";
const char HTTP_NUM[] = "80";
const char SRC[] = "src";
const char HREF[] = "href";
const char SINGLE_SLASH[] = "/";
const char DOUBLE_SLASH[] = "//";

const char SHORT_DESCRIPTION[] = "short description";
const char HELP[] = "help";
const char URL[] = "url";
const char DEPTH[] = "depth";
const char NETWORK_THREADS[] = "network_threads";
const char PARSER_THREADS[] = "parser_threads";
const char OUTPUT[] = "output";

const char info_HELP[] = "адрес HTML страницы";
const char info_URL[] = "адрес HTML страницы";
const char info_DEPTH[] = "адрес HTML страницы";
const char info_NETWORK_THREADS[] = "адрес HTML страницы";
const char info_PARSER_THREADS[] = "адрес HTML страницы";
const char info_OUTPUT[] = "адрес HTML страницы";

const char err_msg[] = "err_format";

const char output_format[] = "[%Severity%] %Message%";

struct address
{
    string  port;
    string  host;
    string  target;
} typedef address;

struct task{
    task(string _html, address _address)
    {
        addr = std::move(_address);
        html = std::move(_html);
    }
    address addr;
    string html;
} typedef task;

class crawler{
public:
    void start(const string &link, unsigned depth,
               unsigned network_threads, unsigned parser_threads,
               const string &dir);

    void producers_loop(unsigned network_threads, unsigned depth);

    void consumers_loop();

    int producer(const string &link);
    int consumer(const task &page);

    void search_for_links(GumboNode* node, const address &addr);

    void search_for_images(GumboNode* node, const task &page);


private:
    string get_port_from_link(const string &request);
    string get_host_from_link(const string &request);
    string get_target_from_link(const string &request);

    string create_link(GumboAttribute* sth, const address &link);
    string create_img(GumboAttribute* sth, const address &link);

    string download_http(const address &addr);
    string download_https(const address &addr);

    address create_address(const string &link);

    bool add_link_for_search(const string &link);

    bool add_img_to_output(const string &link);

    void log_init(const string &dir);

    boost::asio::io_context ioc;
    ssl::context ctx{ ssl::context::sslv23_client };

    queue<string> future_storing;
    queue<string> links_to_download;
    vector<string> links_history;
    queue<task> to_search_images;
    vector<string> output;

    mutex history_mutex;
    mutex to_download_mutex;
    mutex future_mutex;
    mutex output_mutex;
    mutex to_search_images_mutex;

    atomic_uint check = 0;

    atomic_bool end_network = false;
};

#endif // INCLUDE_HEADER_HPP_
