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

#include "config.h"
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <boost/property_tree/json_parser.hpp>
#include <openssl/evp.h>

#ifdef _WIN32
#include <wincrypt.h>
#include <tchar.h>
#endif // _WIN32
#ifdef __APPLE__
#include <Security/Security.h>
#endif // __APPLE__
#include <openssl/opensslv.h>
#include "ssl/ssldefaults.h"
#include "ssl/sslsession.h"
#include "core/utils.h"
#include "session/session.h"

using namespace std;
using namespace boost::property_tree;
using namespace boost::asio::ssl;

void Config::load(const string &filename) {
    ptree tree;
    read_json(filename, tree);
    populate(tree);
}

void Config::populate(const string &JSON) {
    compare_hash = get_hashCode(JSON);
    istringstream s(JSON);
    ptree tree;
    read_json(s, tree);
    populate(tree);
}

void Config::populate(const ptree &tree) {
    string rt = tree.get("run_type", string("client"));
    if (rt == "server") {
        run_type = SERVER;
    } else if (rt == "forward") {
        run_type = FORWARD;
    } else if (rt == "nat") {
        run_type = NAT;
    } else if (rt == "client") {
        run_type = CLIENT;
    } else if (rt == "client_tun") {
        run_type = CLIENT_TUN;
    } else if (rt == "server_tun") {
        run_type = SERVERT_TUN;
    } else {
        throw runtime_error("wrong run_type in config file");
    }
    local_addr = tree.get("local_addr", string());
    local_port = tree.get("local_port", uint16_t());
    remote_addr = tree.get("remote_addr", string());
    remote_port = tree.get("remote_port", uint16_t());
    target_addr = tree.get("target_addr", string());
    target_port = tree.get("target_port", uint16_t());
    map<string, string>().swap(password);
    if (tree.get_child_optional("password")) {
        for (auto& item: tree.get_child("password")) {
            string p = item.second.get_value<string>();
            password[SHA224(p)] = p;
        }
    }
    udp_timeout = tree.get("udp_timeout", 60);
    udp_socket_buf = tree.get("udp_socket_buf", -1);
    udp_recv_buf = tree.get("udp_socket_buf", int(Session::MAX_BUF_LENGTH));
    log_level = static_cast<Log::Level>(tree.get("log_level", int(1)));
    ssl.verify = tree.get("ssl.verify", true);
    ssl.verify_hostname = tree.get("ssl.verify_hostname", true);
    ssl.cert = tree.get("ssl.cert", string());
    ssl.key = tree.get("ssl.key", string());
    ssl.key_password = tree.get("ssl.key_password", string());
    ssl.cipher = tree.get("ssl.cipher", string());
    ssl.cipher_tls13 = tree.get("ssl.cipher_tls13", string());
    ssl.prefer_server_cipher = tree.get("ssl.prefer_server_cipher", true);
    ssl.sni = tree.get("ssl.sni", string());
    ssl.alpn = "";
    if (tree.get_child_optional("ssl.alpn")) {
        for (auto& item: tree.get_child("ssl.alpn")) {
            string proto = item.second.get_value<string>();
            ssl.alpn += (char)((unsigned char)(proto.length()));
            ssl.alpn += proto;
        }
    }
    map<string, uint16_t>().swap(ssl.alpn_port_override);
    if (tree.get_child_optional("ssl.alpn_port_override")) {
        for (auto& item: tree.get_child("ssl.alpn_port_override")) {
            ssl.alpn_port_override[item.first] = item.second.get_value<uint16_t>();
        }
    }
    ssl.reuse_session = tree.get("ssl.reuse_session", true);
    ssl.session_ticket = tree.get("ssl.session_ticket", false);
    ssl.session_timeout = tree.get("ssl.session_timeout", long(600));
    ssl.plain_http_response = tree.get("ssl.plain_http_response", string());
    ssl.curves = tree.get("ssl.curves", string());
    ssl.dhparam = tree.get("ssl.dhparam", string());
    tcp.prefer_ipv4 = tree.get("tcp.prefer_ipv4", false);
    tcp.no_delay = tree.get("tcp.no_delay", true);
    tcp.keep_alive = tree.get("tcp.keep_alive", true);
    tcp.reuse_port = tree.get("tcp.reuse_port", false);
    tcp.fast_open = tree.get("tcp.fast_open", false);
    tcp.fast_open_qlen = tree.get("tcp.fast_open_qlen", 20);
    tcp.connect_time_out = tree.get("tcp.connect_time_out", 10);
    mysql.enabled = tree.get("mysql.enabled", false);
    mysql.server_addr = tree.get("mysql.server_addr", string("127.0.0.1"));
    mysql.server_port = tree.get("mysql.server_port", uint16_t(3306));
    mysql.database = tree.get("mysql.database", string("trojan"));
    mysql.username = tree.get("mysql.username", string("trojan"));
    mysql.password = tree.get("mysql.password", string());
    mysql.cafile = tree.get("mysql.cafile", string());
    experimental.pipeline_num = tree.get("experimental.pipeline_num", uint32_t(0));
    experimental.pipeline_ack_window = tree.get("experimental.pipeline_ack_window", uint32_t(200));
    experimental.pipeline_loadbalance_configs.clear();
    experimental._pipeline_loadbalance_configs.clear();
    experimental._pipeline_loadbalance_context.clear();
    if(tree.get_child_optional("experimental.pipeline_loadbalance_configs")){
        if(experimental.pipeline_num == 0){
            _log_with_date_time("Pipeline load balance need to enable pipeline (set pipeline_num as non zero)", Log::ERROR);
        }else{
            for (auto &item : tree.get_child("experimental.pipeline_loadbalance_configs")){
                auto config = item.second.get_value<string>();
                experimental.pipeline_loadbalance_configs.emplace_back(config);
            }

            std::string tmp;
            _log_with_date_time("Pipeline will use load balance config:", Log::WARN);
            for (auto it = experimental.pipeline_loadbalance_configs.begin();
                it != experimental.pipeline_loadbalance_configs.end(); it++) {
                
                auto other = make_shared<Config>();
                other->load(*it);

                auto ssl = make_shared<boost::asio::ssl::context>(context::sslv23);
                other->prepare_ssl_context(*ssl, tmp);

                experimental._pipeline_loadbalance_configs.emplace_back(other);
                experimental._pipeline_loadbalance_context.emplace_back(ssl);
                _log_with_date_time("Loaded " + (*it) + " config.", Log::WARN);
            }
        }        
    }
    experimental.pipeline_proxy_icmp = tree.get("experimental.pipeline_proxy_icmp", false);

    tun.tun_name = tree.get("tun.tun_name", "");
    tun.net_ip = tree.get("tun.net_ip", "");
    tun.net_mask = tree.get("tun.net_mask", "");
    tun.mtu = tree.get("tun.mtu", uint16_t(1500));
    tun.tun_fd = tree.get("tun.tun_fd", int(-1));
}

bool Config::sip003() {
    char *JSON = getenv("SS_PLUGIN_OPTIONS");
    if (JSON == nullptr) {
        return false;
    }
    populate(JSON);
    switch (run_type) {
        case SERVER:
            local_addr = getenv("SS_REMOTE_HOST");
            local_port = atoi(getenv("SS_REMOTE_PORT"));
            break;
        case CLIENT:
        case NAT:
        case CLIENT_TUN:
        case SERVERT_TUN:
            throw runtime_error("SIP003 with wrong run_type");
        case FORWARD:
            remote_addr = getenv("SS_REMOTE_HOST");
            remote_port = atoi(getenv("SS_REMOTE_PORT"));
            local_addr = getenv("SS_LOCAL_HOST");
            local_port = atoi(getenv("SS_LOCAL_PORT"));
            break;

    }
    return true;
}

void Config::prepare_ssl_context(boost::asio::ssl::context& ssl_context, string& plain_http_response){

    Log::level = log_level;
    auto native_context = ssl_context.native_handle();
    ssl_context.set_options(context::default_workarounds | context::no_sslv2 | context::no_sslv3 | context::single_dh_use);
    if (ssl.curves != "") {
        SSL_CTX_set1_curves_list(native_context, ssl.curves.c_str());
    }
    if (run_type == Config::SERVER) {
        ssl_context.use_certificate_chain_file(ssl.cert);
        ssl_context.set_password_callback([this](size_t, context_base::password_purpose) {
            return this->ssl.key_password;
        });
        ssl_context.use_private_key_file(ssl.key, context::pem);
        if (ssl.prefer_server_cipher) {
            SSL_CTX_set_options(native_context, SSL_OP_CIPHER_SERVER_PREFERENCE);
        }
        if (ssl.alpn != "") {
            SSL_CTX_set_alpn_select_cb(native_context, [](SSL*, const unsigned char **out, unsigned char *outlen, const unsigned char *in, unsigned int inlen, void *config) -> int {
                if (SSL_select_next_proto((unsigned char**)out, outlen, (unsigned char*)(((Config*)config)->ssl.alpn.c_str()), (unsigned int)((Config*)config)->ssl.alpn.length(), in, inlen) != OPENSSL_NPN_NEGOTIATED) {
                    return SSL_TLSEXT_ERR_NOACK;
                }
                return SSL_TLSEXT_ERR_OK;
            }, this);
        }
        if (ssl.reuse_session) {
            SSL_CTX_set_timeout(native_context, ssl.session_timeout);
            if (!ssl.session_ticket) {
                SSL_CTX_set_options(native_context, SSL_OP_NO_TICKET);
            }
        } else {
            SSL_CTX_set_session_cache_mode(native_context, SSL_SESS_CACHE_OFF);
            SSL_CTX_set_options(native_context, SSL_OP_NO_TICKET);
        }

        if (ssl.plain_http_response != "") {
            ifstream ifs(ssl.plain_http_response, ios::binary);
            if (!ifs.is_open()) {
                throw runtime_error(ssl.plain_http_response + ": " + strerror(errno));
            }
            plain_http_response = string(istreambuf_iterator<char>(ifs), istreambuf_iterator<char>());
        }
        if (ssl.dhparam == "") {
            ssl_context.use_tmp_dh(boost::asio::const_buffer(SSLDefaults::g_dh2048_sz, SSLDefaults::g_dh2048_sz_size));
        } else {
            ssl_context.use_tmp_dh_file(ssl.dhparam);
        }

    } else {
        if (ssl.sni == "") {
            ssl.sni = remote_addr;
        }
        if (ssl.verify) {
            ssl_context.set_verify_mode(verify_peer);
            if (ssl.cert == "") {
                ssl_context.set_default_verify_paths();
#ifdef _WIN32
                HCERTSTORE h_store = CertOpenSystemStore(0, _T("ROOT"));
                if (h_store) {
                    X509_STORE *store = SSL_CTX_get_cert_store(native_context);
                    PCCERT_CONTEXT p_context = NULL;
                    while ((p_context = CertEnumCertificatesInStore(h_store, p_context))) {
                        const unsigned char *encoded_cert = p_context->pbCertEncoded;
                        X509 *x509 = d2i_X509(NULL, &encoded_cert, p_context->cbCertEncoded);
                        if (x509) {
                            X509_STORE_add_cert(store, x509);
                            X509_free(x509);
                        }
                    }
                    CertCloseStore(h_store, 0);
                }
#endif // _WIN32
#ifdef __APPLE__
                SecKeychainSearchRef pSecKeychainSearch = NULL;
                SecKeychainRef pSecKeychain;
                OSStatus status = noErr;
                X509 *cert = NULL;

                // Leopard and above store location
                status = SecKeychainOpen ("/System/Library/Keychains/SystemRootCertificates.keychain", &pSecKeychain);
                if (status == noErr) {
                    X509_STORE *store = SSL_CTX_get_cert_store(native_context);
                    status = SecKeychainSearchCreateFromAttributes (pSecKeychain, kSecCertificateItemClass, NULL, &pSecKeychainSearch);
                     for (;;) {
                        SecKeychainItemRef pSecKeychainItem = nil;

                        status = SecKeychainSearchCopyNext (pSecKeychainSearch, &pSecKeychainItem);
                        if (status == errSecItemNotFound) {
                            break;
                        }

                        if (status == noErr) {
                            void *_pCertData;
                            UInt32 _pCertLength;
                            status = SecKeychainItemCopyAttributesAndData (pSecKeychainItem, NULL, NULL, NULL, &_pCertLength, &_pCertData);

                            if (status == noErr && _pCertData != NULL) {
                                unsigned char *ptr;

                                ptr = (unsigned char *)_pCertData;       /*required because d2i_X509 is modifying pointer */
                                cert = d2i_X509 (NULL, (const unsigned char **) &ptr, _pCertLength);
                                if (cert == NULL) {
                                    continue;
                                }

                                if (!X509_STORE_add_cert (store, cert)) {
                                    X509_free (cert);
                                    continue;
                                }
                                X509_free (cert);

                                status = SecKeychainItemFreeAttributesAndData (NULL, _pCertData);
                            }
                        }
                        if (pSecKeychainItem != NULL) {
                            CFRelease (pSecKeychainItem);
                        }
                    }
                    CFRelease (pSecKeychainSearch);
                    CFRelease (pSecKeychain);
                }
#endif // __APPLE__
            } else {
                ssl_context.load_verify_file(ssl.cert);
            }
            if (ssl.verify_hostname) {
#if BOOST_VERSION >= 107300
                ssl_context.set_verify_callback(host_name_verification(ssl.sni));
#else
                ssl_context.set_verify_callback(rfc2818_verification(ssl.sni));
#endif
            }
            X509_VERIFY_PARAM *param = X509_VERIFY_PARAM_new();
            X509_VERIFY_PARAM_set_flags(param, X509_V_FLAG_PARTIAL_CHAIN);
            SSL_CTX_set1_param(native_context, param);
            X509_VERIFY_PARAM_free(param);
        } else {
            ssl_context.set_verify_mode(verify_none);
        }
        if (ssl.alpn != "") {
            SSL_CTX_set_alpn_protos(native_context, (unsigned char*)(ssl.alpn.c_str()), (unsigned int)ssl.alpn.length());
        }
        if (ssl.reuse_session) {
            SSL_CTX_set_session_cache_mode(native_context, SSL_SESS_CACHE_CLIENT);
            SSLSession::set_callback(native_context);
            if (!ssl.session_ticket) {
                SSL_CTX_set_options(native_context, SSL_OP_NO_TICKET);
            }
        } else {
            SSL_CTX_set_options(native_context, SSL_OP_NO_TICKET);
        }
    }

    if (ssl.cipher != "") {
        SSL_CTX_set_cipher_list(native_context, ssl.cipher.c_str());
    }

    if (ssl.cipher_tls13 != "") {
#ifdef ENABLE_TLS13_CIPHERSUITES
        SSL_CTX_set_ciphersuites(native_context, ssl.cipher_tls13.c_str());
#else  // ENABLE_TLS13_CIPHERSUITES
        _log_with_date_time("TLS1.3 ciphersuites are not supported", Log::WARN);
#endif // ENABLE_TLS13_CIPHERSUITES
    }
    
    if (Log::keylog) {
#ifdef ENABLE_SSL_KEYLOG
        SSL_CTX_set_keylog_callback(native_context, [](const SSL*, const char *line) {
            fprintf(Log::keylog, "%s\n", line);
            fflush(Log::keylog);
        });
#else // ENABLE_SSL_KEYLOG
        _log_with_date_time("SSL KeyLog is not supported", Log::WARN);
#endif // ENABLE_SSL_KEYLOG
    }
}

void Config::prepare_ssl_reuse(boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& socket) const {
    auto ssl_handle = socket.native_handle();
    if (!ssl.sni.empty()) {
        SSL_set_tlsext_host_name(ssl_handle, ssl.sni.c_str());
    }
    if (ssl.reuse_session) {
        SSL_SESSION *session = SSLSession::get_session();
        if (session) {
            SSL_set_session(ssl_handle, session);
        }
    }
}

string Config::SHA224(const string &message) {
    uint8_t digest[EVP_MAX_MD_SIZE];
    char mdString[(EVP_MAX_MD_SIZE << 1) + 1];
    unsigned int digest_len;
    EVP_MD_CTX *ctx;
    if ((ctx = EVP_MD_CTX_new()) == nullptr) {
        throw runtime_error("could not create hash context");
    }
    if (!EVP_DigestInit_ex(ctx, EVP_sha224(), nullptr)) {
        EVP_MD_CTX_free(ctx);
        throw runtime_error("could not initialize hash context");
    }
    if (!EVP_DigestUpdate(ctx, message.c_str(), message.length())) {
        EVP_MD_CTX_free(ctx);
        throw runtime_error("could not update hash");
    }
    if (!EVP_DigestFinal_ex(ctx, digest, &digest_len)) {
        EVP_MD_CTX_free(ctx);
        throw runtime_error("could not output hash");
    }

    for (unsigned int i = 0; i < digest_len; ++i) {
        sprintf(mdString + (i << 1), "%02x", (unsigned int)digest[i]);
    }
    mdString[digest_len << 1] = '\0';
    EVP_MD_CTX_free(ctx);
    return string(mdString);
}
