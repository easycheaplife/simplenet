#!/bin/bash
g++ -g -o server epoll_reactor.cc event_loop.cc select_reactor.cc server.cc -lpthread
g++ -g -o echo_server epoll_reactor.cc event_loop.cc select_reactor.cc echo_server.cc -lpthread
g++ -g -o client test_client.cc
