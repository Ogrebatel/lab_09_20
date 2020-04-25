// Copyright 2018 Your Name <your_email>

#include <header.hpp>

void crawler::start(const string &link, unsigned depth,
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

void crawler::producers_loop(unsigned network_threads, unsigned depth) {
    for (unsigned i = 0; i < depth; ++i)
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
        ++check;
        if (check == network_threads)
        {
            future_storing.swap(links_to_download);
            check = 0;
        } else {
            while (check != 0){}
        }
    }
    end_network = true;
}

void crawler::consumers_loop()
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
}

int crawler::producer(const string &link){
    try
    {
        address addr = create_address(link);
        string ending =
                (addr.port == HTTPS_NUM)?
                download_https(addr) : download_http(addr);
        GumboOutput* Output = gumbo_parse(ending.c_str());
        search_for_links(Output->root, addr);
        gumbo_destroy_output(&kGumboDefaultOptions, Output);

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

int crawler::consumer(const task &page){
    try
    {
        GumboOutput* Output = gumbo_parse(page.html.c_str());
        search_for_images(Output->root, page);
        gumbo_destroy_output(&kGumboDefaultOptions, Output);
    }
    catch (std::exception const& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

void crawler::search_for_links(GumboNode* node, const address &addr) {
    if (node->type != GUMBO_NODE_ELEMENT) {
        return;
    }

    GumboAttribute* sth;

    if (node->v.element.tag == GUMBO_TAG_A &&
        (sth = gumbo_get_attribute(&node->v.element.attributes,
                                   HREF)))
    {
        string link = create_link(sth, addr);
        if (link == err_msg) return;
        if (!add_link_for_search(link)) return;
    }

    GumboVector* children = &node->v.element.children;

    for (unsigned int i = 0; i < children->length; ++i) {
        search_for_links(static_cast<GumboNode*>(children->data[i]),
                         addr);
    }
}

void crawler::search_for_images(GumboNode* node, const task &page) {
    if (node->type != GUMBO_NODE_ELEMENT) return;

    GumboAttribute* sth;

    if (node->v.element.tag == GUMBO_TAG_IMG &&
        (sth = gumbo_get_attribute(
                &node->v.element.attributes, SRC)))
    {
        string img = create_img(sth, page.addr);
        if (img == err_msg) return;
        if (add_img_to_output(img))
            BOOST_LOG_TRIVIAL(trace) << img << endl;
    }
    GumboVector* children = &node->v.element.children;

    for (unsigned int i = 0; i < children->length; ++i) {
        search_for_images(static_cast<GumboNode*>(
                                  children->data[i]), page);
    }
}

string crawler::get_port_from_link(const string &request)
{
    return (request.find(HTTPS) == 0)? HTTPS_NUM : HTTP_NUM;
}

string crawler::get_host_from_link(const string &request)
{
    string result = request.substr(request.find(DOUBLE_SLASH) + 2);
    return result.substr(0, result.find(SINGLE_SLASH));
}

string crawler::get_target_from_link(const string &request)
{
    string result = request.substr(request.find(DOUBLE_SLASH) + 2);
    return result.substr(result.find(SINGLE_SLASH));
}

string crawler::create_link(GumboAttribute* sth, const address &link)
{
    string tmp(sth->value);
    if (tmp.find(HTTP) == 0){
        return tmp;
    } else if (tmp.find(DOUBLE_SLASH) == 0) {
        if (link.port == HTTPS_NUM) {
            tmp = "https:" + tmp;
        } else {
            tmp = "http:" + tmp;
        }
        return tmp;

    } else if (tmp.find(SINGLE_SLASH) == 0){
        if (link.port == HTTPS_NUM) {
            tmp = "https://" + link.host + tmp;
        } else {
            tmp = "http://" + link.host + tmp;
        }
        return tmp;
    }
    return err_msg;
}
string crawler::create_img(GumboAttribute* sth, const address &link) {
    string tmp(sth->value);
    if (tmp.find(HTTP) == 0){
        return tmp;
    } else if (tmp.find(DOUBLE_SLASH) == 0) {
        if (link.port == HTTPS_NUM) {
            tmp = "https:" + tmp;

        } else {
            tmp = "http:" + tmp;
        }
        return tmp;

    } else if (tmp.find(SINGLE_SLASH) == 0){
        if (link.port == HTTPS_NUM) {
            tmp = "https://" + link.host + tmp;
        } else {
            tmp = "http://" + link.host + tmp;
        }
        return tmp;
    }
    return err_msg;
}

string crawler::download_http(const address &addr){
    boost::asio::ip::tcp::resolver resolver(ioc);
    boost::beast::tcp_stream stream(ioc);
    auto const results = resolver.resolve(addr.host, addr.port);
    stream.connect(results);
    boost::beast::http::request<boost::beast::http::string_body>
            req{boost::beast::http::verb::get, addr.target, 11};
    req.set(boost::beast::http::field::host, addr.host);
    req.set(boost::beast::http::field::user_agent,
            BOOST_BEAST_VERSION_STRING);
    boost::beast::http::write(stream, req);
    boost::beast::flat_buffer buffer;
    boost::beast::http::response<boost::beast::http::dynamic_body> res;
    boost::beast::http::read(stream, buffer, res);
    stream.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both);
    return boost::beast::buffers_to_string(res.body().data());
}

string crawler::download_https(const address &addr){
    load_root_certificates(ctx);
    tcp::resolver resolver{ioc};

    ssl::stream<tcp::socket> stream{ioc, ctx};

    auto const results = resolver.resolve(addr.host, addr.port);
    boost::asio::connect(stream.next_layer(),
                         results.begin(), results.end());
    stream.handshake(ssl::stream_base::client);

    http::request<http::string_body>
            req{http::verb::get, addr.target, 10};
    req.set(http::field::host, addr.host);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    http::write(stream, req);
    boost::beast::flat_buffer buffer;
    http::response<http::dynamic_body> res;
    http::read(stream, buffer, res);
    return boost::beast::buffers_to_string(res.body().data());
}
address crawler::create_address(const string &link){
    address result;
    result.host = get_host_from_link(link);
    result.port = get_port_from_link(link);
    result.target = get_target_from_link(link);
    return result;
}

bool crawler::add_link_for_search(const string &link)
{
    history_mutex.lock();
    auto _check = std::find(links_history.begin(),
                            links_history.end(), link);
    if (_check == links_history.end()){
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

bool crawler::add_img_to_output(const string &link) {
    output_mutex.lock();
    auto _check = std::find(output.begin(),
                            output.end(), link);
    if (_check == output.end()) {
        output.push_back(link);
        output_mutex.unlock();
        return true;
    }
    output_mutex.unlock();
    return false;
}


void crawler::log_init(const string &dir)
{
    boost::log::add_file_log // расширенная настройка
            (boost::log::keywords::file_name = dir.c_str(),
             boost::log::keywords::format =
                     output_format);

    boost::log::add_console_log
            (
                    std::cout,
                    boost::log::keywords::format =
                            output_format);
    boost::log::add_common_attributes();
}

int main(int argc, char **argv) {
    po::options_description desc(SHORT_DESCRIPTION);
    desc.add_options()
            (HELP, info_HELP)
            (URL, po::value<string>(),
             info_URL)
            (DEPTH, po::value<unsigned>(),
             info_DEPTH)
            (NETWORK_THREADS, po::value<unsigned>(),
             info_NETWORK_THREADS)
            (PARSER_THREADS, po::value<unsigned>(),
             info_PARSER_THREADS)
            (OUTPUT, po::value<string>(),
             info_OUTPUT);

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
    if (vm.count(HELP)) {
        cout << desc << endl;
        return 1;
    }
    if (!vm.count(URL)
        || !vm.count(DEPTH)
        || !vm.count(NETWORK_THREADS)
        || !vm.count(PARSER_THREADS)
        || !vm.count(OUTPUT)) {
        cout << "error: bad format" << endl << desc << endl;
        return 1;
    }

    string url = vm[URL].as<string>();
    unsigned depth = vm[DEPTH].as<unsigned>();
    unsigned network_threads = vm[NETWORK_THREADS].as<unsigned>();
    unsigned parser_threads = vm[PARSER_THREADS].as<unsigned>();
    string output = vm[OUTPUT].as<string>();

    crawler a;
    a.start(url, depth, network_threads, parser_threads, output);
    return 0;
}
