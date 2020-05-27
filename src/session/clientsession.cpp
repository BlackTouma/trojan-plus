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

#include "clientsession.h"

#include "core/service.h"
#include "core/utils.h"
#include "proto/trojanrequest.h"
#include "proto/udppacket.h"
#include "ssl/sslsession.h"

using namespace std;
using namespace boost::asio::ip;
using namespace boost::asio::ssl;

ClientSession::ClientSession(Service* _service, const Config& config, context &ssl_context) :
    SocketSession(_service, config),
    status(HANDSHAKE),
    is_udp(false),
    first_packet_recv(false),
    in_socket(_service->service()),
    out_socket(_service->service(), ssl_context){
    pipeline_com.allocate_session_id();
}

ClientSession::~ClientSession(){
    pipeline_com.free_session_id(); 
}

tcp::socket& ClientSession::accept_socket() {
    return in_socket;
}

bool ClientSession::prepare_session(){
    boost::system::error_code ec;
    start_time = time(nullptr);
    in_endpoint = in_socket.remote_endpoint(ec);
    if (ec) {
        _log_with_date_time("cannot get in_endpoint in prepare_session", Log::FATAL);
        destroy();
        return false;
    }
    config.prepare_ssl_reuse(out_socket);
    return true;
}

void ClientSession::start() {
    if(prepare_session()){
        in_async_read();
    }
}
void ClientSession::recv_ack_cmd(){
    SocketSession::recv_ack_cmd();
    if(pipeline_com.is_wait_for_pipeline_ack()){
        in_async_read();
    }
}

void ClientSession::in_async_read() {
    if(pipeline_com.is_using_pipeline() && status == FORWARD){
        if(!pipeline_com.pre_call_ack_func()){
            _log_with_endpoint(in_endpoint, "Cannot ClientSession::in_async_read ! Is waiting for ack");
            return;
        }
        _log_with_endpoint(in_endpoint, "Permit to ClientSession::in_async_read! ack:" + to_string(pipeline_com.pipeline_ack_counter));
    }

    auto self = shared_from_this();
    in_socket.async_read_some(boost::asio::buffer(in_read_buf, MAX_BUF_LENGTH), [this, self](const boost::system::error_code error, size_t length) {
        if (error == boost::asio::error::operation_aborted) {
            return;
        }
        if (error) {
            output_debug_info_ec(error);
            destroy();
            return;
        }
        in_recv(string((const char*)in_read_buf, length));
    });
}

void ClientSession::in_async_write(const string &data) {
    auto self = shared_from_this();
    auto data_copy = make_shared<string>(data);
    boost::asio::async_write(in_socket, boost::asio::buffer(*data_copy), [this, self, data_copy](const boost::system::error_code error, size_t) {
        if (error) {
            output_debug_info_ec(error);
            destroy();
            return;
        }

        if(pipeline_com.is_using_pipeline() && status == FORWARD){
            service->session_async_send_to_pipeline(*this, PipelineRequest::ACK, "", [this, self](const boost::system::error_code error) {
                if (error) {
                    output_debug_info_ec(error);
                    destroy();
                    return;
                }

                in_sent();
            });
        }else{
            in_sent();
        }
    });
}

void ClientSession::out_async_read() {
    if(pipeline_com.is_using_pipeline()){
        pipeline_com.pipeline_data_cache.async_read([this](const string &data) {
            out_recv(data);
        });
    }else{
        auto self = shared_from_this();
        out_socket.async_read_some(boost::asio::buffer(out_read_buf, MAX_BUF_LENGTH), [this, self](const boost::system::error_code error, size_t length) {
            if (error) {
                output_debug_info_ec(error);
                destroy();
                return;
            }
            out_recv(string((const char*)out_read_buf, length));
        });
    }
}

void ClientSession::out_async_write(const string &data) {
    auto self = shared_from_this();
    if(pipeline_com.is_using_pipeline()){
        service->session_async_send_to_pipeline(*this, PipelineRequest::DATA, data, [this, self](const boost::system::error_code error) {
            if (error) {
                output_debug_info_ec(error);
                destroy();
                return;
            }
            out_sent();
        });
    }else{        
        auto data_copy = make_shared<string>(data);
        boost::asio::async_write(out_socket, boost::asio::buffer(*data_copy), [this, self, data_copy](const boost::system::error_code error, size_t) {
            if (error) {
                output_debug_info_ec(error);
                destroy();
                return;
            }
            out_sent();
        });
    }
    
}

void ClientSession::udp_async_read() {
    auto self = shared_from_this();
    udp_socket.async_receive_from(boost::asio::buffer(udp_read_buf, MAX_BUF_LENGTH), udp_recv_endpoint, [this, self](const boost::system::error_code error, size_t length) {
        if (error == boost::asio::error::operation_aborted) {
            return;
        }
        if (error) {
            output_debug_info_ec(error);
            destroy();
            return;
        }
        udp_recv(string((const char*)udp_read_buf, length), udp_recv_endpoint);
    });
}

void ClientSession::udp_async_write(const string &data, const udp::endpoint &endpoint) {
    auto self = shared_from_this();
    auto data_copy = make_shared<string>(data);
    udp_socket.async_send_to(boost::asio::buffer(*data_copy), endpoint, [this, self, data_copy](const boost::system::error_code error, size_t) {
        if (error) {
            output_debug_info_ec(error);
            destroy();
            return;
        }
        udp_sent();
    });
}

void ClientSession::in_recv(const string &data) {
    switch (status) {
        case HANDSHAKE: {
            if (data.length() < 2 || data[0] != 5 || data.length() != (unsigned int)(unsigned char)data[1] + 2) {
                _log_with_endpoint(in_endpoint, "session_id: " + to_string(get_session_id()) + " unknown protocol", Log::ERROR);
                destroy();
                return;
            }
            bool has_method = false;
            for (int i = 2; i < data[1] + 2; ++i) {
                if (data[i] == 0) {
                    has_method = true;
                    break;
                }
            }
            if (!has_method) {
                _log_with_endpoint(in_endpoint, "session_id: " + to_string(get_session_id()) + " unsupported auth method", Log::ERROR);
                in_async_write(string("\x05\xff", 2));
                status = INVALID;
                return;
            }
            in_async_write(string("\x05\x00", 2));
            break;
        }
        case REQUEST: {
            if (data.length() < 7 || data[0] != 5 || data[2] != 0) {
                _log_with_endpoint(in_endpoint, "session_id: " + to_string(get_session_id()) + " bad request", Log::ERROR);
                destroy();
                return;
            }
            out_write_buf = config.password.cbegin()->first + "\r\n" + data[1] + data.substr(3) + "\r\n";
            TrojanRequest req;
            if (req.parse(out_write_buf) == -1) {
                _log_with_endpoint(in_endpoint, "session_id: " + to_string(get_session_id()) + " unsupported command", Log::ERROR);
                in_async_write(string("\x05\x07\x00\x01\x00\x00\x00\x00\x00\x00", 10));
                status = INVALID;
                return;
            }
            is_udp = req.command == TrojanRequest::UDP_ASSOCIATE;
            if (is_udp) {
                in_udp_endpoint = udp::endpoint(in_socket.local_endpoint().address(), 0);
                boost::system::error_code ec;
                udp_socket.open(in_udp_endpoint.protocol(), ec);
                if (ec) {
                    output_debug_info();
                    destroy();
                    return;
                }
                udp_socket.bind(in_udp_endpoint);
                
                _log_with_endpoint(in_endpoint, "session_id: " + to_string(get_session_id()) + " requested UDP associate to " + req.address.address + ':' + to_string(req.address.port) + ", open UDP socket " + udp_socket.local_endpoint().address().to_string() + ':' + to_string(udp_socket.local_endpoint().port()) + " for relay", Log::INFO);
                in_async_write(string("\x05\x00\x00", 3) + SOCKS5Address::generate(udp_socket.local_endpoint()));
            } else {
                _log_with_endpoint(in_endpoint, "session_id: " + to_string(get_session_id()) + " requested connection to " + req.address.address + ':' + to_string(req.address.port), Log::INFO);
                in_async_write(string("\x05\x00\x00\x01\x00\x00\x00\x00\x00\x00", 10));
            }
            break;
        }
        case CONNECT: {
            sent_len += data.length();
            first_packet_recv = true;
            out_write_buf += data;
            break;
        }
        case FORWARD: {
            sent_len += data.length();
            out_async_write(data);
            break;
        }
        case UDP_FORWARD: {
            _log_with_endpoint(in_endpoint, "session_id: " + to_string(get_session_id()) + " unexpected data from TCP port", Log::ERROR);
            destroy();
            break;
        }
        default: break;
    }
}

void ClientSession::in_sent() {
    switch (status) {
        case HANDSHAKE: {
            status = REQUEST;
            in_async_read();
            break;
        }
        case REQUEST: {
            status = CONNECT;
            in_async_read();
            if (is_udp) {
                udp_async_read();
            }
            request_remote();
            break;
        }
        case FORWARD: {
            out_async_read();
            break;
        }
        case INVALID: {
            output_debug_info();
            destroy();
            break;
        }
        default: break;
    }
}

void ClientSession::request_remote(){
    auto self = shared_from_this();
    auto cb = [this, self](){
        boost::system::error_code ec;
        if (is_udp) {
            if (!first_packet_recv) {
                udp_socket.cancel(ec);
            }
            status = UDP_FORWARD;
        } else {
            if (!first_packet_recv) {
                in_socket.cancel(ec);
            }
            status = FORWARD;
        }
        out_async_read();
        out_async_write(out_write_buf);
    };

    if(pipeline_com.is_using_pipeline()){
        cb();
    }else{  
        connect_remote_server_ssl(this, config.remote_addr, to_string(config.remote_port), resolver, out_socket, in_endpoint,cb);
    }    
}

void ClientSession::out_recv(const string &data) {
    if (status == FORWARD) {
        recv_len += data.length();
        in_async_write(data);
    } else if (status == UDP_FORWARD) {
        udp_data_buf += data;
        udp_sent();
    }
}

void ClientSession::out_sent() {
    if (status == FORWARD) {
        in_async_read();
    } else if (status == UDP_FORWARD) {
        udp_async_read();
    }
}

void ClientSession::udp_recv(const string &data, const udp::endpoint&) {
    if (data.length() == 0) {
        return;
    }
    if (data.length() < 3 || data[0] || data[1] || data[2]) {
        _log_with_endpoint(in_udp_endpoint, "session_id: " + to_string(get_session_id()) + " bad UDP packet", Log::ERROR);
        destroy();
        return;
    }
    SOCKS5Address address;
    size_t address_len;
    bool is_addr_valid = address.parse(data.substr(3), address_len);
    if (!is_addr_valid) {
        _log_with_endpoint(in_udp_endpoint, "session_id: " + to_string(get_session_id()) + " bad UDP packet", Log::ERROR);
        destroy();
        return;
    }
    size_t length = data.length() - 3 - address_len;
    _log_with_endpoint(in_udp_endpoint, "session_id: " + to_string(get_session_id()) + " sent a UDP packet of length " + to_string(length) + " bytes to " + address.address + ':' + to_string(address.port));
    string packet = data.substr(3, address_len) + char(uint8_t(length >> 8)) + char(uint8_t(length & 0xFF)) + "\r\n" + data.substr(address_len + 3);
    sent_len += length;
    if (status == CONNECT) {
        first_packet_recv = true;
        out_write_buf += packet;
    } else if (status == UDP_FORWARD) {
        out_async_write(packet);
    }
}

void ClientSession::udp_sent() {
    if (status == UDP_FORWARD) {
        UDPPacket packet;
        size_t packet_len;
        bool is_packet_valid = packet.parse(udp_data_buf, packet_len);
        if (!is_packet_valid) {
            if (udp_data_buf.length() > MAX_BUF_LENGTH) {
                _log_with_endpoint(in_udp_endpoint, "session_id: " + to_string(get_session_id()) + " UDP packet too long", Log::ERROR);
                destroy();
                return;
            }
            out_async_read();
            return;
        }
        _log_with_endpoint(in_udp_endpoint, "session_id: " + to_string(get_session_id()) + " received a UDP packet of length " + to_string(packet.length) + " bytes from " + packet.address.address + ':' + to_string(packet.address.port));
        SOCKS5Address address;
        size_t address_len;
        bool is_addr_valid = address.parse(udp_data_buf, address_len);
        if (!is_addr_valid) {
            _log_with_endpoint(in_udp_endpoint, "session_id: " + to_string(get_session_id()) + " udp_sent: invalid UDP packet address", Log::ERROR);
            destroy();
            return;
        }
        string reply = string("\x00\x00\x00", 3) + udp_data_buf.substr(0, address_len) + packet.payload;
        udp_data_buf = udp_data_buf.substr(packet_len);
        recv_len += packet.length;
        udp_async_write(reply, udp_recv_endpoint);
    }
}



void ClientSession::destroy(bool pipeline_call /*= false*/) {
    if (status == DESTROY) {
        return;
    }
    status = DESTROY;
    _log_with_endpoint(in_endpoint, "session_id: " + to_string(get_session_id()) + " disconnected, " + to_string(recv_len) + " bytes received, " + to_string(sent_len) + " bytes sent, lasted for " + to_string(time(nullptr) - start_time) + " seconds", Log::INFO);
    boost::system::error_code ec;
    resolver.cancel();
    if (in_socket.is_open()) {
        in_socket.cancel(ec);
        in_socket.shutdown(tcp::socket::shutdown_both, ec);
        in_socket.close(ec);
    }
    if (udp_socket.is_open()) {
        udp_socket.cancel(ec);
        udp_socket.close(ec);
    }

    shutdown_ssl_socket(this, out_socket);

    if(!pipeline_call && pipeline_com.is_using_pipeline()){
        service->session_destroy_in_pipeline(*this);
    }
}
