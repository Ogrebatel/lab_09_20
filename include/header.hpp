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
#include "root_certificates.h"
#include <cstdlib>
#include <string>
#include <queue>
#include <thread>

using tcp = boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;
namespace http = boost::beast::http;
namespace po = boost::program_options;

using namespace std;


struct address
{
    string  port;
    string  host;
    string  target;
} typedef address;

struct task{
    task(string _html, address _address)
    {
        addr = _address;
        html = _html;
    }
    address addr;
    string html;
} typedef task;

class crawler{
public:
    crawler()
    {

    }

    void start(const string &link, unsigned depth,
            unsigned network_threads, unsigned parser_threads,
            const string &dir)
    {
        vector<thread> network;
        vector<thread> parser;
        links_to_download.push(link);

        log_init(dir);

        for (unsigned i = 0; i < network_threads; ++i)
            network.emplace_back(thread(&crawler::producers_loop,
                                 this, network_threads, depth));

        for (unsigned i = 0; i < parser_threads - 1; ++i)
            parser.emplace_back(thread(&crawler::consumers_loop, this));

        consumers_loop();

        for (unsigned i = 0; i < network_threads; ++i)
            network[i].join();

        for (unsigned i = 0; i < parser_threads - 1; ++i)
            parser[i].join();
    }

    void producers_loop(unsigned network_threads, unsigned depth) {
        for (int i = 0; i < depth; ++i)
        {
            while (!links_to_download.empty())
            {
                //
                to_download_mutex.lock();
                string link = links_to_download.front();
                links_to_download.pop();
                to_download_mutex.unlock();
                //
                producer(link);

            }
            cout << "AAAAAAAAAAAAAAAAAAA";
            ++check;
            if (check == network_threads)
            {
                future_storing.swap(links_to_download);
                check = 0;
            }
            else while(check != 0){}

        }
        end_network = true;
    }

    void consumers_loop()
    {
        while (!to_search_images.empty() || !end_network)
        {
            to_search_images_mutex.lock();
            if (to_search_images.empty())
            {
                to_search_images_mutex.unlock();
                continue;
            }
            //
            task page = to_search_images.front();
            to_search_images.pop();
            to_search_images_mutex.unlock();
            consumer(page);
            //
        }
    };

    int producer(const string &link){
        try
        {
            address addr = create_address(link);
            string ending = download_page(addr);
            GumboOutput* output = gumbo_parse(ending.c_str());
            search_for_links(output->root, addr);
            gumbo_destroy_output(&kGumboDefaultOptions, output);

            to_search_images_mutex.lock();
            to_search_images.emplace(ending, addr);
            to_search_images_mutex.unlock();

        }
        catch (std::exception const& e)
        {
            BOOST_LOG_TRIVIAL(error) << e.what() << endl;
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    int consumer(const task &page){
        try
        {
            GumboOutput* output = gumbo_parse(page.html.c_str());
            search_for_images(output->root, page);
            gumbo_destroy_output(&kGumboDefaultOptions, output);
        }
        catch (std::exception const& e)
        {
            std::cerr << "Error: " << e.what() << std::endl;
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }


    void search_for_links(GumboNode* node, const address &addr) {
        if (node->type != GUMBO_NODE_ELEMENT) {
            return;
        }

        GumboAttribute* sth;

        if (node->v.element.tag == GUMBO_TAG_A &&
            (sth = gumbo_get_attribute(&node->v.element.attributes,
                    "href")))
        {
            string link = create_link(sth, addr);
            if (link == "0") return;
            if(!add_link_for_search(link)) return;
        }

        GumboVector* children = &node->v.element.children;

        for (unsigned int i = 0; i < children->length; ++i) {
            search_for_links(static_cast<GumboNode*>(children->data[i]),
                    addr);
        }
    }


    void search_for_images(GumboNode* node, const task &page) {
        if (node->type != GUMBO_NODE_ELEMENT) return;

        GumboAttribute* sth;

        if (node->v.element.tag == GUMBO_TAG_IMG &&
            (sth = gumbo_get_attribute(
                    &node->v.element.attributes, "src")))
        {
            string img = create_img(sth, page.addr);
            if (img == "0") return;
            if(add_img_to_output(img))
                BOOST_LOG_TRIVIAL(trace) << img << endl;
        }
        GumboVector* children = &node->v.element.children;

        for (unsigned int i = 0; i < children->length; ++i) {
            search_for_images(static_cast<GumboNode*>(
                    children->data[i]), page);
        }
    }


private:
    string get_port_from_link(const string &request)
    {
        return (request.find("https")==0)? "443" : "80";
    }
    string get_host_from_link(const string &request)
    {
        string result = request.substr(request.find("//") + 2);
        return result.substr(0, result.find('/'));
    }
    string get_target_from_link(const string &request)
    {
        string result = request.substr(request.find("//") + 2);
        return result.substr(result.find('/'));
    }

    string create_link(GumboAttribute* sth, const address &link)
    {
        string tmp(sth->value);
        if (tmp.find("http") == 0){
            return tmp;
        }
        else if(tmp.find("//")==0) {
            if(link.port == "443") tmp = "https:" + tmp;
            else tmp = "http:" + tmp;
            return tmp;

        } else if (tmp.find("/")==0){
            if (link.port == "443") tmp = "https://" + link.host + tmp;
            else tmp = "http://" + link.host + tmp;
            return tmp;
        }
        return "0";
    }
    string create_img(GumboAttribute* sth, const address &link) {
        string tmp(sth->value);
        if (tmp.find("http") == 0){
            return tmp;
        }
        else if(tmp.find("//")==0) {
            if(link.port == "443") tmp = "https:" + tmp;
            else tmp = "http:" + tmp;
            return tmp;

        } else if (tmp.find("/")==0){
            if (link.port == "443") tmp = "https://" + link.host + tmp;
            else tmp = "http://" + link.host + tmp;
            return tmp;
        }
        return "0";
    }

    string download_page(const address &addr){
        load_root_certificates(ctx);
        tcp::resolver resolver{ ioc };
        ssl::stream<tcp::socket> stream{ ioc, ctx };

        auto const results = resolver.resolve(addr.host, addr.port);
        boost::asio::connect(stream.next_layer(),
                results.begin(), results.end());
        stream.handshake(ssl::stream_base::client);

        http::request<http::string_body>
            req{http::verb::get, addr.target, 11};
        req.set(http::field::host, addr.host);
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
        http::write(stream, req);
        boost::beast::flat_buffer buffer;
        http::response<http::dynamic_body> res;
        http::read(stream, buffer, res);
        return boost::beast::buffers_to_string(res.body().data());
    }
    address create_address(const string &link){
        address result;
        result.host = get_host_from_link(link);
        result.port = get_port_from_link(link);
        result.target = get_target_from_link(link);
        return result;
    };

    bool add_link_for_search(const string &link)
    {
        history_mutex.lock();
        auto check = std::find(links_history.begin(),
                links_history.end(), link);
        if (check == links_history.end()){
            links_history.push_back(link);
            history_mutex.unlock();

            future_mutex.lock();
            future_storing.push(link);
            future_mutex.unlock();

            return true;
        }
        history_mutex.unlock();
        return false;
    }

    bool add_img_to_output(const string &link) {
        output_mutex.lock();
        auto check = std::find(output.begin(),
                output.end(), link);
        if (check == output.end()) {

            output.push_back(link);
            output_mutex.unlock();
            return true;
        }
        output_mutex.unlock();
        return false;
    }


    void log_init(const string &dir)
    {
        boost::log::add_file_log // расширенная настройка
                (boost::log::keywords::file_name = dir.c_str(),
                        boost::log::keywords::format =
                                "[%Severity%] %Message%");

        boost::log::add_console_log
                (
                        std::cout,
                        boost::log::keywords::format =
                                "[%Severity%] %Message%");
        boost::log::add_common_attributes();
    }

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

    atomic_int check = 0;

    atomic_bool end_network = false;

};


int main(int argc, char **argv) {
    po::options_description desc("short description");
    desc.add_options()
            ("help,h", "0 помощи")
            ("url", po::value<string>(),
                    "адрес HTML страницы")
            ("depth", po::value<unsigned>(),
                    "глубина поиска по странице")
            ("network_threads", po::value<unsigned>(),
                    "количество потоков для скачивания страниц")
            ("parser_threads", po::value<unsigned>(),
                    "количество потоков для обработки страниц")
            ("output", po::value<string>(),
                    "путь до выходного файла");

    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);
    }

    catch (po::error &e) {
        cout << e.what() << endl;
        cout << desc << endl;
        return 1;
    }
    if (vm.count("help")) {
        cout << desc << endl;
        return 1;
    }
    if (!vm.count("url")
        || !vm.count("depth")
        || !vm.count("network_threads")
        || !vm.count("parser_threads")
        || !vm.count("output")) {
        cout << "error: bad format" << endl << desc << endl;
        return 1;
    }

    string url = vm["url"].as<string>();
    unsigned depth = vm["depth"].as<unsigned>();
    unsigned network_threads = vm["network_threads"].as<unsigned>();
    unsigned parser_threads = vm["parser_threads"].as<unsigned>();
    string output = vm["output"].as<string>();


    crawler a;
    a.start(url, depth, network_threads, parser_threads, output);
    return 0;
}

#endif // INCLUDE_HEADER_HPP_
