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

#include <sslcom.hpp>
#include <tcpcom.hpp>

#include <sockshostcx.hpp>
#include <socksproxy.hpp>
#include <mitmhost.hpp>
#include <cfgapi.hpp>


SocksProxy::SocksProxy(baseCom* c): MitmProxy(c) {}
SocksProxy::~SocksProxy() {}

void SocksProxy::on_left_message(baseHostCX* basecx) {

    socksServerCX* cx = static_cast<socksServerCX*>(basecx);
    if(cx != nullptr) {
        if(cx->state_ == WAIT_POLICY) {
            DIAS_("SocksProxy::on_left_message: policy check: accepted");
            std::vector<baseHostCX*> l;
            std::vector<baseHostCX*> r;
            l.push_back(cx);
            r.push_back(cx->right);
            
            
            cfgapi_write_lock.lock();
            matched_policy(cfgapi_obj_policy_match(l,r));
            bool verdict = cfgapi_obj_policy_action(matched_policy());
            
            PolicyRule* p = nullptr;
            if(matched_policy() >= 0) {
                p = cfgapi_obj_policy.at(matched_policy());
            }

            DIA_("socksProxy::on_left_message: policy check result: policy# %d policyid 0x%x verdict %s", matched_policy(), p, verdict ? "accept" : "reject" );
            
            cfgapi_write_lock.unlock();
            
            socks5_policy s5_verdict = verdict ? ACCEPT : REJECT;
            cx->verdict(s5_verdict);
        }
        else if(cx->state_ == HANDOFF) {
            DIAS_("SocksProxy::on_left_message: socksHostCX handoff msg received");
            cx->state(ZOMBIE);
            
            socks5_handoff(cx);
        } else {

            WARS_("SocksProxy::on_left_message: unknown message");
        }
    }
}

void SocksProxy::socks5_handoff(socksServerCX* cx) {

    DEBS_("SocksProxy::socks5_handoff: start");
    
    if(matched_policy() < 0) {
        DIA_("SocksProxy::sock5_handoff: matching policy: %d: dropping.",matched_policy());
        dead(true);
        return;
    } 
    else if(matched_policy() >= (signed int)cfgapi_obj_policy.size()) {
        DIA_("SocksProxy::sock5_handoff: matching policy out of policy index table: %d/%d: dropping.",matched_policy(),cfgapi_obj_policy.size());
        dead(true);
        return;
    }
    
    ////// we matched the policy
    
    int s = cx->socket();
    bool ssl = false;
    
    baseCom* new_com = nullptr;
    switch(cx->com()->nonlocal_dst_port()) {
        case 443:
        case 465:
        case 636:
        case 993:
        case 995:
            new_com = new socksSSLMitmCom();
            ssl = true;
            break;
        default:
            new_com = new socksTCPCom();
    }
    new_com->master(com()->master());
    
    MitmHostCX* n_cx = new MitmHostCX(new_com, s);
    n_cx->waiting_for_peercom(true);
    n_cx->com()->name();
    n_cx->name();
    n_cx->com()->nonlocal_dst(true);
    n_cx->com()->nonlocal_dst_host() = cx->com()->nonlocal_dst_host();
    n_cx->com()->nonlocal_dst_port() = cx->com()->nonlocal_dst_port();
    n_cx->com()->nonlocal_dst_resolved(true);
//     n_cx->writebuf()->append(cx->writebuf()->data(),cx->writebuf()->size());
    
    // get rid of it
    //cx->socket(0);
    cx->remove_socket();
    if(cx->left) { 
        // we are using the socket, so we don't want it to be cleared in cx->left destructor.
        cx->left->remove_socket();
    }
    
    delete cx;
    
    left_sockets.clear();
    ldaadd(n_cx);
    n_cx->on_delay_socket(s);
    
    MitmHostCX *target_cx = new MitmHostCX(n_cx->com()->slave(), n_cx->com()->nonlocal_dst_host().c_str(), 
                                        string_format("%d",n_cx->com()->nonlocal_dst_port()).c_str()
                                        );
    std::string h;
    std::string p;
    n_cx->name();
    n_cx->com()->resolve_socket_src(n_cx->socket(),&h,&p);
    n_cx->host() = h;
    n_cx->port() = p;
    
    
    n_cx->peer(target_cx);
    target_cx->peer(n_cx);

    cfgapi_write_lock.lock();
    if(cfgapi_obj_policy.at(matched_policy())->nat == POLICY_NAT_NONE) {
        target_cx->com()->nonlocal_src(true);
        target_cx->com()->nonlocal_src_host() = h;
        target_cx->com()->nonlocal_src_port() = std::stoi(p);
    }
    cfgapi_write_lock.unlock();
    
    n_cx->matched_policy(matched_policy());
    target_cx->matched_policy(matched_policy());
        
    int real_socket = target_cx->connect(false);
    com()->set_monitor(real_socket);
    com()->set_poll_handler(real_socket,this);
    
    if(ssl) {
//         ((SSLCom*)n_cx->com())->upgrade_server_socket(n_cx->socket());
        DEBS_("SocksProxy::socks5_handoff: mark1");        
        
//         ((SSLCom*)target_cx->com())->upgrade_client_socket(target_cx->socket());
    }
    
    radd(target_cx);
    
    if (cfgapi_obj_policy_apply(n_cx,this) < 0) {
        // strange, but it can happen if the sockets is closed between policy match and this profile application
        // mark dead.
        INFS_("SocksProxy::socks5_handoff: session failed policy application");
        dead(true);
    } else {

        baseHostCX* src_cx = n_cx;
        bool delete_proxy = false;
        // apply policy and get result

        std::string target_host = n_cx->com()->nonlocal_dst_host();
        short unsigned int target_port = n_cx->com()->nonlocal_dst_port();

        if (src_cx != nullptr) {

            // resolve source information - is there an identity info for that IP?
            if (opt_auth_authenticate || opt_auth_resolve) {

                DIAS___("authentication required or optionally resolved");

                // reload table and check timeouts each 5 seconds
                time_t now = time(nullptr);
                if (now > auth_table_refreshed + 5) {
                    cfgapi_auth_shm_ip_table_refresh();
                    cfgapi_auth_shm_ip6_table_refresh();
                    auth_table_refreshed = now;

                    //one day this can run in separate thread to not slow down session setup rate
                    cfgapi_ip_auth_timeout_check();
                    cfgapi_ip6_auth_timeout_check();
                }

                bool identity_resolved = resolve_identity(src_cx, false);


                if (!identity_resolved) {
                    // identity is unknown!

                    if(opt_auth_authenticate) {
                        if (target_port != 80 && target_port != 443) {
                            delete_proxy = true;
                            INF___("Connection %s closed: authorization failed (unknown identity)",
                                   n_cx->c_name());
                        }
                    }
                    else {
                        DIA___("Connection %s: authentication info optional, continuing.",n_cx->c_name());
                    }

                } else if(opt_auth_authenticate) {


                    bool bad_auth = true;

                    // investigate L3 protocol
                    int af = AF_INET;
                    if (n_cx->com()) {
                        af = n_cx->com()->l3_proto();
                    }
                    std::string str_af = inet_family_str(af);


                    cfgapi_identity_ip_lock.lock();

                    // use common base pointer, so we can use all IdentityInfo types
                    IdentityInfoBase *id_ptr = nullptr;

                    if (af == AF_INET || af == 0) {
                        auto ip = auth_ip_map.find(n_cx->host());
                        if (ip != auth_ip_map.end()) {
                            id_ptr = &(*ip).second;
                        }
                    } else if (af == AF_INET6) {
                        auto ip = auth_ip6_map.find(n_cx->host());
                        if (ip != auth_ip6_map.end()) {
                            id_ptr = &(*ip).second;
                        }
                    }

                    if (id_ptr != nullptr) {
                        //std::string groups = id_ptr->last_logon_info.groups();

                        if (cfgapi_obj_policy_profile_auth(matched_policy()) != nullptr)
                            for (auto i: cfgapi_obj_policy_profile_auth(matched_policy())->sub_policies) {
                                for (auto x: id_ptr->groups_vec) {
                                    DEB___("Connection identities: ip identity '%s' against policy '%s'", x.c_str(),
                                           i->name.c_str());
                                    if (x == i->name) {
                                        DIA___("Connection identities: ip identity '%s' matches policy '%s'", x.c_str(),
                                               i->name.c_str());
                                        bad_auth = false;
                                    }
                                }
                            }
                        if (bad_auth) {
                            if (target_port != 80 && target_port != 443) {
                                INF___("Connection %s closed: authorization failed (non-matching identity criteria)",
                                       n_cx->c_name());
                            } else {
                                INF___("Connection %s closed: authorization failed (non-matching identity criteria)(with replacement)",
                                       n_cx->c_name());
                                // set bad_auth true, because despite authentication failed, it could be replaced (we can let user know
                                // he is not allowed to proceed
                                bad_auth = false;
                                auth_block_identity = true;
                            }
                        }
                    }
                    cfgapi_identity_ip_lock.unlock();

                    if (bad_auth) {
                        delete_proxy = true;
                    }
                }
            } else {
                DIA___("Connection %s: authentication info optional, continuing.",n_cx->c_name());
            }
        }


        if(delete_proxy) {
            DEB___("deleting proxy %s", c_name());
            dead(true);
        }
    }


    DIAS_("SocksProxy::socks5_handoff: finished");
}







baseHostCX* MitmSocksProxy::new_cx(int s) {
    auto r = new socksServerCX(com()->slave(),s);
    return r; 
}

void MitmSocksProxy::on_left_new(baseHostCX* just_accepted_cx) {

    SocksProxy* new_proxy = new SocksProxy(com()->slave());
    // let's add this just_accepted_cx into new_proxy
    std::string h;
    std::string p;
    just_accepted_cx->name();
    just_accepted_cx->com()->resolve_socket_src(just_accepted_cx->socket(),&h,&p);

    new_proxy->ladd(just_accepted_cx);
    this->proxies().insert(new_proxy);
    DEBS_("MitmMasterProxy::on_left_new: finished");
}
