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
    
*/    

#include <mitmhost.hpp>
#include <display.hpp>
#include <logger.hpp>
#include <cfgapi.hpp>

std::vector<duplexFlowMatch*> sigs_starttls;
std::vector<duplexFlowMatch*> sigs_detection;

baseCom* MySSLMitmCom::replicate() { 
    return new SSLMitmCom(); 
}

bool MySSLMitmCom::spoof_cert(X509* x, SpoofOptions& spo) {
    std::string cert = SSLCertStore::print_cert(x);
    log().append("\n ==== Server certificate:\n" + cert  + "\n ====\n");
    
    bool r = SSLMitmCom::spoof_cert(x,spo);
    
    EXT_("MySSLMitmCom::spoof_cert: cert:\n%s",cert.c_str());
    
    return r;
}


MitmHostCX::MitmHostCX(baseCom* c, const char* h, const char* p ) : AppHostCX::AppHostCX(c,h,p) {
    DEB_("MitmHostCX: constructor %s:%s",h,p);
    load_signatures();
};

MitmHostCX::MitmHostCX( baseCom* c, int s ) : AppHostCX::AppHostCX(c,s) {
    DEB_("MitmHostCX: constructor %d",s);
    load_signatures();
};

int MitmHostCX::process() {

    // incoming data are in the readbuf
    unsigned char *ptr = baseHostCX::readbuf()->data();
    unsigned int len = baseHostCX::readbuf()->size();
    
    // our only processing: hex dup the payload to the log
    DUMS_("Incoming data(" + this->name() + "):\n" +hex_dump(ptr,len));
    
    //  read buffer will be truncated by 'len' bytes. Note: truncated bytes are LOST.
    return len;
};

void MitmHostCX::load_signatures() {
    
    DEBS_("MitmHostCX::load_signatures: start");
    
    zip_signatures(starttls_sensor(),sigs_starttls);
    zip_signatures(sensor(),sigs_detection);
    
    DEBS_("MitmHostCX::load_signatures: stop");
};

void MitmHostCX::on_detect(duplexFlowMatch* x_sig, flowMatchState& s, vector_range& r) {
    
    MyDuplexFlowMatch* sig_sig = (MyDuplexFlowMatch*)x_sig;

    WAR_("Connection from %s matching signature: cat='%s', name='%s'",this->full_name('L').c_str(), sig_sig->category.c_str(), sig_sig->name().c_str());
    DEB_("Connection from %s matching signature: cat='%s', name='%s' at %s",this->full_name('L').c_str(), sig_sig->category.c_str(), sig_sig->name().c_str(), vrangetos(r).c_str());
    
    this->log().append( string_format("\nDetected application: cat='%s', name='%s'\n",sig_sig->category.c_str(), sig_sig->name().c_str()));
    
    
    if(sig_sig->category ==  "www" && sig_sig->name() == "http/get|post") {

      if(r.size() > 0) {
	std::pair<unsigned char,buffer*>& get = flow().flow()[0];
	std::pair<unsigned char,buffer*>& status = flow().flow()[0];
	
	buffer* buffer_get = get.second;
	buffer* buffer_status = status.second;
	std::string buffer_data_string((const char*)buffer_get->data(),0,buffer_get->size());
	
	//INFS_(std::string((const char*)buffer_get->data(),0,buffer_get->size()));
	std::regex re_get("(GET|POST) *([^ \?]+)([^ ]+)");
	std::smatch m_get;
		

	std::regex re_host("Host: *([^ ]+)\r\n");
	std::smatch m_host;
	
	
	std::string str_temp;
	std::string print_request;
	
	if(std::regex_search (buffer_data_string, m_host, re_host))  {
	    if(m_host.size() > 0) {
		str_temp = m_host[1].str();
		print_request += str_temp;

		if(request == nullptr) {
		    request = new app_HttpRequest;
		}
		
		app_HttpRequest* app_request = dynamic_cast<app_HttpRequest*>(request);
		if(app_request != nullptr) {
		  app_request->host = str_temp;
		}
	    }     
	}
	if(std::regex_search (buffer_data_string, m_get, re_get)) {
	    if(m_get.size() > 1) {
		str_temp = m_get[2].str();
		print_request += str_temp;
		
		if(request == nullptr) {
		    request = new app_HttpRequest;
		}
		
		app_HttpRequest* app_request = dynamic_cast<app_HttpRequest*>(request);
		if(app_request != nullptr) {
		  app_request->uri = str_temp;
		}		
		
		if(m_get.size() > 2) {
		    str_temp = m_get[3].str();
		    app_request->params = str_temp;
		
		    //print_request += str_temp;
		}
	    }
	}
	
	INF_("Connection www request: %s",print_request.c_str());
	
	// this is the right way
	// replacement(REPLACE_REDIRECT);
	// replacement(REPLACE_BLOCK);
      }
    }
}

void MitmHostCX::on_starttls() {

    DIAS_("we should now handover myself to SSL worker");
    
    // we know this side is client
//         delete ();
//         delete peercom();

    com_ = new MySSLMitmCom();
    baseCom* pcom = new MySSLMitmCom();
    
    //com()->init(this);
    
    peer()->com(pcom);
    peer(peer()); // this will re-init
    peer()->peer(this);
    
    DIAS_("peers set");
    
    // set flag to wait for the peer to finish spoofing
    
    paused(true);
    
    
    
    ((SSLCom*)peercom())->upgrade_client_socket(peer()->socket());
    ((SSLCom*)com())->upgrade_server_socket(socket());        
    
    cfgapi_obj_policy_apply_tls(matched_policy(),com());
    cfgapi_obj_policy_apply_tls(matched_policy(),peercom());
    
    log().append("\n STARTTLS: plain connection upgraded to SSL/TLS, continuing with inspection.\n\n");
    
    DIAS_("on_starttls finished");   
}


