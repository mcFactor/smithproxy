"""
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
    """

import logging
import socket

import SOAPpy
from M2Crypto import SSL
import pylibconfig2 as cfg

class BendBroker:

    def __init__(self,tenant_index=0,tenant_name=None):
        
        self.tenant_index = int(tenant_index) 
        self.tenant_name = "default"
        if tenant_name:
            self.tenant_name = tenant_name
        
        self.cert_file = '/etc/smithproxy/certs/default/srv-cert.pem'
        self.key_file = '/etc/smithproxy/certs/default/srv-key.pem'

        self.context = SSL.Context()
        self.context.load_cert(self.cert_file,keyfile=self.key_file)

        self.service_port = 65000 + self.tenant_index
        self.bend_port = 64000 + self.tenant_index

        self.l_server = SOAPpy.ThreadingSOAPServer(("0.0.0.0", self.service_port),ssl_context = self.context)
        self.r_server = SOAPpy.SOAPProxy("http://localhost:%d/" % (self.bend_port,))

        self.l_server.registerFunction( SOAPpy.MethodSig(self.ping, keywords=0, context=1) )
        self.l_server.registerFunction( SOAPpy.MethodSig(self.whoami, keywords=0, context=1) )
        self.l_server.registerFunction( SOAPpy.MethodSig(self.authenticate, keywords=0, context=1) )
        
        self.l_server.registerFunction( SOAPpy.MethodSig(self.admin_login, keywords=0, context=1) )
        self.l_server.registerFunction( SOAPpy.MethodSig(self.admin_token_list, keywords=0, context=1) )
        
        self.l_server.registerFunction( SOAPpy.MethodSig(self.admin_keepalive, keywords=0, context=1) )
        self.l_server.registerFunction( SOAPpy.MethodSig(self.admin_logout, keywords=0, context=1) )


        self.create_logger()
        self.load_config()

    def create_logger(self):
        self.log = logging.getLogger('bendbro')
        hdlr = logging.FileHandler("/var/log/smithproxy_bendbro.%s.log" % (self.tenant_name,))
        formatter = logging.Formatter('%(asctime)s [%(process)d] [%(levelname)s] %(message)s')
        hdlr.setFormatter(formatter)
        self.log.addHandler(hdlr) 
        self.log.setLevel(logging.INFO)        

    def load_config(self):
        self.cfg = cfg.Config()
        self.cfg.read_file("/etc/smithproxy/smithproxy.cfg")    


    """ return addresses where real cotact can be done """
    def ping(self,_SOAPContext = None):
        portal_address = self.cfg.settings.auth_portal.address
        portal_port = self.service_port
        fqdn = socket.getfqdn()
        
        s = "https://%s:%s/" % (portal_address,portal_port)
        sq = "https://%s:%s/" % (fqdn,portal_port)
        
        r = [s,sq]
        
        return r
        


    def whoami(self,_SOAPContext = None):
        if _SOAPContext:
            ip = _SOAPContext.connection.getpeername()[0]
            return self.r_server.whois(ip)

        return []


    def authenticate(self, username, password, _SOAPContext = None):
        if _SOAPContext:
            ip = _SOAPContext.connection.getpeername()[0]
            return self.r_server.authenticate(ip,username,password,"0")

        return False,"http://auth-portal-url/"

    def admin_login(self, username, password, _SOAPContext = None):
        if _SOAPContext:
            ip = _SOAPContext.connection.getpeername()[0]
            return self.r_server.admin_login(username,password,ip)
        
        return -1
    
    def admin_token_list(self, admin_token, _SOAPContext = None):
        return self.r_server.admin_token_list(admin_token)
        
    def admin_keepalive(self, admin_token, _SOAPContext = None):
        return self.r_server.admin_keepalive(admin_token)

    def admin_logout(self, admin_token, _SOAPContext = None):
        return self.r_server.admin_logout(admin_token)


    def run(self):
        self.log.warning("Backend broker daemon started (tenant name %s, index %d)" % (self.tenant_name,self.tenant_index) )
        self.log.info("listening on port: %d" % (self.service_port,) )
        self.log.info("backend port set to: %d" % (self.bend_port,) )
        self.l_server.serve_forever()


if __name__ == "__main__":
    b = BendBroker(0,"default")
    b.run()
