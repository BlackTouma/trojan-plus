/*
 * This file is part of the trojan project.
 * Trojan is an unidentifiable mechanism that helps you bypass GFW.
 * Copyright (C) 2017-2020  The Trojan Authors.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <cstdint>
#include <map>
#include <boost/property_tree/ptree.hpp>
#include <boost/asio/ssl.hpp>
#include "log.h"

class Config {
public:
    enum RunType {
        SERVER,
        CLIENT,
        FORWARD,
        NAT,

        CLIENT_TUN,
        SERVERT_TUN
    } run_type;
    std::string local_addr;
    uint16_t local_port;
    std::string remote_addr;
    uint16_t remote_port;
    std::string target_addr;
    uint16_t target_port;
    std::map<std::string, std::string> password;
    int udp_timeout;
    Log::Level log_level;
    class SSLConfig {
    public:
        bool verify;
        bool verify_hostname;
        std::string cert;
        std::string key;
        std::string key_password;
        std::string cipher;
        std::string cipher_tls13;
        bool prefer_server_cipher;
        std::string sni;
        std::string alpn;
        std::map<std::string, uint16_t> alpn_port_override;
        bool reuse_session;
        bool session_ticket;
        long session_timeout;
        std::string plain_http_response;
        std::string curves;
        std::string dhparam;
    } ssl;
    class TCPConfig {
    public:
        bool prefer_ipv4;
        bool no_delay;
        bool keep_alive;
        bool reuse_port;
        bool fast_open;
        int fast_open_qlen;
        int connect_time_out;
    } tcp;
    class MySQLConfig {
    public:
        bool enabled;
        std::string server_addr;
        uint16_t server_port;
        std::string database;
        std::string username;
        std::string password;
        std::string cafile;
    } mysql;

    struct Experimental{
        uint32_t pipeline_num;
        uint32_t pipeline_ack_window;
        std::vector<std::string> pipeline_loadbalance_configs;
        std::vector<std::shared_ptr<Config>> _pipeline_loadbalance_configs;
        std::vector<std::shared_ptr<boost::asio::ssl::context>> _pipeline_loadbalance_context;
        bool pipeline_proxy_icmp;
    } experimental;

    struct TUN{
        std::string tun_name;
        std::string net_ip;
        std::string net_mask;
        int mtu;
        int tun_fd;
    } tun;

    void load(const std::string &filename);
    void populate(const std::string &JSON);
    bool sip003();
    void prepare_ssl_context(boost::asio::ssl::context& ssl_context, std::string& plain_http_response);
    static std::string SHA224(const std::string &message);

    void prepare_ssl_reuse(boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& socket) const;
private:
    void populate(const boost::property_tree::ptree &tree);
};

#endif // _CONFIG_H_
