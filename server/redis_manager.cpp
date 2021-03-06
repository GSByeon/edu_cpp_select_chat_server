//
// Created by jwwoo on 2019-11-06.
//

#include <iostream>
#include <cstring>
#include "redis_manager.hpp"
#include "include/CRedisConn.h"

Server::Error::Code RedisManager::Connect(const std::string& host, unsigned short port) {
    conn_ = new RedisCpp::CRedisConn();

    if (!conn_->connect(host, port)) {
        std::cout << "REDIS: connect error " << conn_->getErrorStr( ) << std::endl;
        return Server::Error::CONNECT_REDIS_FAIL;
    }

    runnable_ = true;

    return Server::Error::NONE;
}

void RedisManager::Run() {
    thread_ = std::thread([this]() -> void { ProcessRedisPacket(); });
}

void RedisManager::End() {
    runnable_ = false;

    if (thread_.joinable())
    {
        thread_.join();
    }

    delete conn_;
}

void RedisManager::AddRedisReqPacketQueue(PacketInfo redis_packet_info) {
    std::unique_lock<std::mutex> lock(req_mutex_);
    req_packet_queue_.emplace_back(redis_packet_info);
    lock.unlock();
}

void RedisManager::AddRedisResPacketQueue(PacketInfo redis_packet_info) {
    std::unique_lock<std::mutex> lock(res_mutex_);
    res_packet_queue_.emplace_back(redis_packet_info);
    lock.unlock();
}

PacketInfo *RedisManager::GetRedisResPacket() {
    if (res_packet_queue_.empty()) {
        return nullptr;
    }

    std::unique_lock<std::mutex> lock(res_mutex_);
    PacketInfo &redis_packet_info = res_packet_queue_.front();
    res_packet_queue_.pop_front();
    lock.unlock();

    return &redis_packet_info;
}

void RedisManager::ProcessRedisPacket() {
    while (runnable_) {
        std::unique_lock<std::mutex> lock(req_mutex_);
        if (!req_packet_queue_.empty()) {
            PacketInfo &req_packet_info = req_packet_queue_.front();
            req_packet_queue_.pop_front();
            lock.unlock();

            if (req_packet_info.packet_id == PacketID::REDIS_LOGIN_REQ) {
                auto login_req = new RedisPacketLoginReq;
                memcpy(login_req, req_packet_info.body, sizeof(RedisPacketLoginReq));

                RedisPacketLoginRes login_res;

                std::string user_id(login_req->user_id);
                std::string user_password(login_req->user_pw);

                if (conn_->get(user_id, user_password)) {
                    login_res.code = Server::Error::Code::NONE;
                    memcpy(login_res.user_id, login_req->user_id, MAX_USER_ID_LENGTH);
                } else {
                    login_res.code = Server::Error::Code::USER_NOT_EXIST;
                }

                PacketInfo res_packet_info;
                res_packet_info.session_index = req_packet_info.session_index;
                res_packet_info.packet_id = PacketID::REDIS_LOGIN_RES;
                res_packet_info.body_size = sizeof(RedisPacketLoginRes);
                res_packet_info.body = new char[res_packet_info.body_size];
                memcpy(res_packet_info.body, reinterpret_cast<char *>(&login_res), res_packet_info.body_size);

                AddRedisResPacketQueue(res_packet_info);
            }
        }
    }
}
