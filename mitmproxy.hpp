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


#ifndef MITMPROXY_HPP
 #define MITMPROXY_HPP

#include <basecom.hpp>
#include <hostcx.hpp>
#include <sockshostcx.hpp>
#include <baseproxy.hpp>
#include <threadedacceptor.hpp>
#include <threadedreceiver.hpp>
#include <traflog.hpp>
#include <policy.hpp>
#include <cfgapi_auth.hpp>


struct whitelist_verify_entry {
};

typedef expiring<whitelist_verify_entry> whitelist_verify_entry_t;

class FilterProxy;

class MitmProxy : public baseProxy, public socle::sobject {
    
protected:
    socle::trafLog *tlog_ = nullptr;
    
    bool write_payload_ = false;
    
    bool identity_resolved_ = false;    // meant if attempt has been done, regardless of it's result.
    bool identity_resolved_time = 0;
    shm_logon_info_base* identity_ = nullptr;
    
    std::vector<ProfileContentRule>* content_rule_ = nullptr; //save some space and store it as a pointer. Init it only when needed and delete in dtor.
    
    int matched_policy_ = -1;
    
public: 
    time_t half_holdtimer = 0;
    static unsigned int half_timeout;
    
    static ptr_cache<std::string,whitelist_verify_entry_t> whitelist_verify;
    
    bool opt_auth_authenticate = false;
    bool opt_auth_resolve = false;
    bool auth_block_identity = false;
    
    
    // Remote filters - use other proxy to filter content of this proxy.
    // Elements are pair of "name" and pointer to the filter proxy 
    std::vector<std::pair<std::string,FilterProxy*>> filters_;
    void add_filter(std::string name, FilterProxy* fp);
    
    // tap proxy - unmonitor all left and right sockets, pause contexts
    virtual void tap();
    // untap proxy - monitor back again all L and R sockets, unpause contexts
    virtual void untap();
    
    
    
    int matched_policy() { return matched_policy_; }
    void matched_policy(int p) { matched_policy_ = p; }    
    
    inline bool identity_resolved();
    inline void identity_resolved(bool b);
    shm_logon_info_base* identity() { return identity_; }
    inline void identity(shm_logon_info_base* i) { if(identity_ != nullptr) { delete identity_; }  if(i != nullptr) { identity_ = i->clone(); } }
    bool resolve_identity(baseHostCX*,bool);
    bool update_auth_ipX_map(baseHostCX*);
    bool apply_id_policies(baseHostCX* cx);
   
    
    bool write_payload(void) { return write_payload_; } 
    void write_payload(bool b) { write_payload_ = b; }
    
    socle::trafLog* tlog() { return tlog_; }
    void toggle_tlog();
    
    explicit MitmProxy(baseCom* c);
    virtual ~MitmProxy();
    
    // this virtual method is called whenever there are new bytes in any LEFT host context!
    virtual void on_left_bytes(baseHostCX* cx);    
    virtual void on_right_bytes(baseHostCX* cx);
    
    // ... and also when there is error on L/R side, claim the proxy DEAD. When marked dead, it will be safely 
    // closed by it's master proxy next cycle.
    
    virtual void on_left_error(baseHostCX* cx);
    virtual void on_right_error(baseHostCX* cx);
    
    // check authentication status and return true if redirected
    virtual void on_half_close(baseHostCX* cx);
    virtual bool handle_authentication(MitmHostCX* cx);
    virtual void handle_replacement_auth(MitmHostCX* cx);
    
    // check sslcom response and return true if redirected
    virtual bool handle_com_response_ssl(MitmHostCX* cx);
    virtual void handle_replacement_ssl(MitmHostCX* cx);
    
    // check if content has been pulled from cache and return true if so
    virtual bool handle_cached_response(MitmHostCX* cx);
    
    virtual bool ask_destroy() { dead(true); return true; };
    virtual std::string to_string(int verbosity=iINF);
    
    virtual int handle_sockets_once(baseCom*);
    
    void init_content_replace();
    std::vector<ProfileContentRule>* content_rule() { return content_rule_; }    
    void content_replace(std::vector<ProfileContentRule>& x) { 
	for(auto i: x) {
	    content_rule_->push_back(i);
	}
    }
    
    buffer content_replace_apply(buffer);
    
    void __debug_zero_connections(baseHostCX* cx);
    
    MitmHostCX* first_left();
    MitmHostCX* first_right();
    
public:

    socle::meter mtr_down;
    socle::meter mtr_up;    
    
    static socle::meter total_mtr_up;
    static socle::meter total_mtr_down;

    
    DECLARE_C_NAME("MitmProxy");
    DECLARE_LOGGING(to_string);
};


class MitmMasterProxy : public ThreadedAcceptorProxy<MitmProxy> {
public:
    
    MitmMasterProxy(baseCom* c, int worker_id) : ThreadedAcceptorProxy< MitmProxy >(c,worker_id) {};
    
    virtual baseHostCX* new_cx(int s);
    virtual void on_left_new(baseHostCX* just_accepted_cx);
    virtual int handle_sockets_once(baseCom* c);
    
    static bool ssl_autodetect;
    static bool ssl_autodetect_harder;
    bool detect_ssl_on_plain_socket(int s);
    
    time_t auth_table_refreshed = 0;
};


class MitmUdpProxy : public ThreadedReceiverProxy<MitmProxy> {
public:
    MitmUdpProxy(baseCom* c, int worker_id) : ThreadedReceiverProxy< MitmProxy >(c,worker_id) {};
    virtual void on_left_new(baseHostCX* just_accepted_cx);
    baseHostCX* new_cx(int s);
};


std::string whitelist_make_key(MitmHostCX*);

#endif //MITMPROXY_HPP
