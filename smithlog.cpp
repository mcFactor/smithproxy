/*
    Smithproxy- transparent proxy with SSL inspection capabilities.
    Copyright (c) 2014, Ales Stibal <astib@mag0.net>, All rights reserved.

    Smithproxy is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Smithproxy is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Smithproxy.  If not, see <http://www.gnu.org/licenses/>.

    Linking Smithproxy statically or dynamically with other modules is
    making a combined work based on Smithproxy. Thus, the terms and
    conditions of the GNU General Public License cover the whole combination.

    In addition, as a special exception, the copyright holders of Smithproxy
    give you permission to combine Smithproxy with free software programs
    or libraries that are released under the GNU LGPL and with code
    included in the standard release of OpenSSL under the OpenSSL's license
    (or modified versions of such code, with unchanged license).
    You may copy and distribute such a system following the terms
    of the GNU GPL for Smithproxy and the licenses of the other code
    concerned, provided that you include the source code of that other code
    when and as the GNU GPL requires distribution of source code.

    Note that people who make modified versions of Smithproxy are not
    obligated to grant this special exception for their modified versions;
    it is their choice whether to do so. The GNU General Public License
    gives permission to release a modified version without this exception;
    this exception also makes it possible to release a modified version
    which carries forward this exception.
*/

#include <display.hpp>
#include <smithlog.hpp>
#include <daemon.hpp>
#include <unistd.h>

QueueLogger::QueueLogger(): logger(), lockable() {
}

int QueueLogger::write_log(loglevel l, std::string& sss) {

    locked_guard<QueueLogger> ll(this);
    
     if(l.level() <= level() || forced_ ) {
        logs_.push(log_entry(l,sss));
     }
    
    if(logs_.size() >= max_len) {
        logs_.pop();
    }
    
    // process on my mark!
    
    return 0;
}

int QueueLogger::write_disk(loglevel l, std::string& sss) {
    locked_guard<QueueLogger> ll(this);
  
    return logger::write_log(l,sss);
}


void QueueLogger::run_queue(QueueLogger* log_src) {

    if(log_src == nullptr) {
        return;
    }
    
    while (!log_src->sig_terminate) {
        log_src->lock();
        
        if(log_src->logs_.size() > 0) {
            log_entry e = log_src->logs_.front(); log_src->logs_.pop();

            //copy elements and unlock before write_log.
            loglevel l = e.first;
            std::string msg = e.second;

            log_src->unlock();
            log_src->write_disk(l, msg);
            
        } else {
            log_src->unlock();
            usleep(10000); // wait 10ms if there is nothing to read
        }
    }
}

std::thread* create_log_writer(logger* log_ptr) {
    std::thread * writer_thread = new std::thread([]() { 
        logger* log_ptr = get_logger();
        QueueLogger* q_logger = dynamic_cast<QueueLogger*>(log_ptr);
        
        if(q_logger != nullptr) {
            QueueLogger::run_queue(q_logger);
        }
    } );
       
    return writer_thread;
}
