// DHCP Library v0.3 - April 25, 2009
// Author: Jordan Terrell - blog.jordanterrell.com

#include "utility/w5100.h"

#include <string.h>
#include <stdlib.h>
#include "Dhcp.h"
#include "Arduino.h"
#include "utility/util.h"

int DhcpClass::beginWithDHCP(uint8_t *mac, unsigned long timeout, unsigned long responseTimeout)
{
    _dhcpLeaseTime=0;
    _dhcpT1=0;
    _dhcpT2=0;
    _lastCheck=0;
    _timeout = timeout;
    _responseTimeout = responseTimeout;

    // zero out _dhcpMacAddr
    memset(_dhcpMacAddr, 0, 6); 
    reset_DHCP_lease();

    memcpy((void*)_dhcpMacAddr, (void*)mac, 6);
    _dhcp_state = STATE_DHCP_START;
    return request_DHCP_lease();
}

void DhcpClass::reset_DHCP_lease(){
    // zero out _dhcpSubnetMask, _dhcpGatewayIp, _dhcpLocalIp, _dhcpDhcpServerIp, _dhcpDnsServerIp
    memset(_dhcpLocalIp, 0, 20);
}

//return:0 on error, 1 if request is sent and response is received
int DhcpClass::request_DHCP_lease(){
    
    uint8_t messageType = 0;
  
    
  
    // Pick an initial transaction ID
    _dhcpTransactionId = random(1UL, 2000UL);
    _dhcpInitialTransactionId = _dhcpTransactionId;

    _dhcpUdpSocket.stop();
    if (_dhcpUdpSocket.begin(DHCP_CLIENT_PORT) == 0)
    {
      // Couldn't get a socket
      return 0;
    }
    
    presend_DHCP();
    
    int result = 0;
    
    unsigned long startTime = millis();
    
    while(_dhcp_state != STATE_DHCP_LEASED)
    {
    	WDT_Restart(WDT); // Watchdog reset
        if(_dhcp_state == STATE_DHCP_START)
        {
            _dhcpTransactionId++;
            
            send_DHCP_MESSAGE(DHCP_DISCOVER, ((millis() - startTime) / 1000));
            _dhcp_state = STATE_DHCP_DISCOVER;
        }
        else if(_dhcp_state == STATE_DHCP_REREQUEST){
            _dhcpTransactionId++;
            send_DHCP_MESSAGE(DHCP_REQUEST, ((millis() - startTime)/1000));
            _dhcp_state = STATE_DHCP_REQUEST;
        }
        else if(_dhcp_state == STATE_DHCP_DISCOVER)
        {
            uint32_t respId;
            messageType = parseDHCPResponse(_responseTimeout, respId);
            if(messageType == DHCP_OFFER)
            {
                // We'll use the transaction ID that the offer came with,
                // rather than the one we were up to
                _dhcpTransactionId = respId;
                send_DHCP_MESSAGE(DHCP_REQUEST, ((millis() - startTime) / 1000));
                _dhcp_state = STATE_DHCP_REQUEST;
            }
        }
        else if(_dhcp_state == STATE_DHCP_REQUEST)
        {
            uint32_t respId;
            messageType = parseDHCPResponse(_responseTimeout, respId);
            if(messageType == DHCP_ACK)
            {
                _dhcp_state = STATE_DHCP_LEASED;
                result = 1;
                //use default lease time if we didn't get it
                if(_dhcpLeaseTime == 0){
                    _dhcpLeaseTime = DEFAULT_LEASE;
                }
                //calculate T1 & T2 if we didn't get it
                if(_dhcpT1 == 0){
                    //T1 should be 50% of _dhcpLeaseTime
                    _dhcpT1 = _dhcpLeaseTime >> 1;
                }
                if(_dhcpT2 == 0){
                    //T2 should be 87.5% (7/8ths) of _dhcpLeaseTime
                    _dhcpT2 = _dhcpT1 << 1;
                }
                _renewInSec = _dhcpT1;
                _rebindInSec = _dhcpT2;
            }
            else if(messageType == DHCP_NAK)
                _dhcp_state = STATE_DHCP_START;
        }
        
        if(messageType == 255)
        {
            messageType = 0;
            _dhcp_state = STATE_DHCP_START;
        }
        
        if(result != 1 && ((millis() - startTime) > _timeout))
            break;
    }
    
    // We're done with the socket now
    _dhcpUdpSocket.stop();
    _dhcpTransactionId++;

    return result;
}

void DhcpClass::presend_DHCP()
{
}

void DhcpClass::send_DHCP_MESSAGE(uint8_t messageType, uint16_t secondsElapsed)
{
    uint8_t buffer[32];
    memset(buffer, 0, 32);
    IPAddress dest_addr( 255, 255, 255, 255 ); // Broadcast address

    if (-1 == _dhcpUdpSocket.beginPacket(dest_addr, DHCP_SERVER_PORT))
    {
        // FIXME Need to return errors
        return;
    }

    buffer[0] = DHCP_BOOTREQUEST;   // op
    buffer[1] = DHCP_HTYPE10MB;     // htype
    buffer[2] = DHCP_HLENETHERNET;  // hlen
    buffer[3] = DHCP_HOPS;          // hops

    // xid
    unsigned long xid = htonl(_dhcpTransactionId);
    memcpy(buffer + 4, &(xid), 4);

    // 8, 9 - seconds elapsed
    buffer[8] = ((secondsElapsed & 0xff00) >> 8);
    buffer[9] = (secondsElapsed & 0x00ff);

    // flags
    unsigned short flags = htons(DHCP_FLAGSBROADCAST);
    memcpy(buffer + 10, &(flags), 2);

    // ciaddr: already zeroed
    // yiaddr: already zeroed
    // siaddr: already zeroed
    // giaddr: already zeroed

    //put data in W5100 transmit buffer
    _dhcpUdpSocket.write(buffer, 28);

    memset(buffer, 0, 32); // clear local buffer

    memcpy(buffer, _dhcpMacAddr, 6); // chaddr

    //put data in W5100 transmit buffer
    _dhcpUdpSocket.write(buffer, 16);

    memset(buffer, 0, 32); // clear local buffer

    // leave zeroed out for sname && file
    // put in W5100 transmit buffer x 6 (192 bytes)
  
    for(int i = 0; i < 6; i++) {
        _dhcpUdpSocket.write(buffer, 32);
    }
  
    // OPT - Magic Cookie
    buffer[0] = (uint8_t)((MAGIC_COOKIE >> 24)& 0xFF);
    buffer[1] = (uint8_t)((MAGIC_COOKIE >> 16)& 0xFF);
    buffer[2] = (uint8_t)((MAGIC_COOKIE >> 8)& 0xFF);
    buffer[3] = (uint8_t)(MAGIC_COOKIE& 0xFF);

    // OPT - message type
    buffer[4] = dhcpMessageType;
    buffer[5] = 0x01;
    buffer[6] = messageType; //DHCP_REQUEST;

    // OPT - client identifier
    buffer[7] = dhcpClientIdentifier;
    buffer[8] = 0x07;
    buffer[9] = 0x01;
    memcpy(buffer + 10, _dhcpMacAddr, 6);

    // OPT - host name
    buffer[16] = hostName;
    buffer[17] = strlen(HOST_NAME) + 6; // length of hostname + last 3 bytes of mac address
    strcpy((char*)&(buffer[18]), HOST_NAME);

    printByte((char*)&(buffer[24]), _dhcpMacAddr[3]);
    printByte((char*)&(buffer[26]), _dhcpMacAddr[4]);
    printByte((char*)&(buffer[28]), _dhcpMacAddr[5]);

    //put data in W5100 transmit buffer
    _dhcpUdpSocket.write(buffer, 30);

    if(messageType == DHCP_REQUEST)
    {
        buffer[0] = dhcpRequestedIPaddr;
        buffer[1] = 0x04;
        buffer[2] = _dhcpLocalIp[0];
        buffer[3] = _dhcpLocalIp[1];
        buffer[4] = _dhcpLocalIp[2];
        buffer[5] = _dhcpLocalIp[3];

        buffer[6] = dhcpServerIdentifier;
        buffer[7] = 0x04;
        buffer[8] = _dhcpDhcpServerIp[0];
        buffer[9] = _dhcpDhcpServerIp[1];
        buffer[10] = _dhcpDhcpServerIp[2];
        buffer[11] = _dhcpDhcpServerIp[3];

        //put data in W5100 transmit buffer
        _dhcpUdpSocket.write(buffer, 12);
    }
    
    buffer[0] = dhcpParamRequest;
    buffer[1] = 0x06;
    buffer[2] = subnetMask;
    buffer[3] = routersOnSubnet;
    buffer[4] = dns;
    buffer[5] = domainName;
    buffer[6] = dhcpT1value;
    buffer[7] = dhcpT2value;
    buffer[8] = endOption;
    
    //put data in W5100 transmit buffer
    _dhcpUdpSocket.write(buffer, 9);

    _dhcpUdpSocket.endPacket();
}

// pav2000 Ждёт ответ до responseTimeout мс и сохраняет ID в transactionId.
// Возвращает тип DHCP-сообщения, 0 при неверном ответе или 255 при тайм-ауте.
uint8_t DhcpClass::parseDHCPResponse(unsigned long responseTimeout, uint32_t& transactionId)
{
    const unsigned long started = millis();
    while (_dhcpUdpSocket.parsePacket() <= 0) {
        if (millis() - started > responseTimeout) return 255;
        delay(50);
    }

    RIP_MSG_FIXED fixed;
    if (_dhcpUdpSocket.read((uint8_t*)&fixed, sizeof(fixed)) != sizeof(fixed) ||
        fixed.op != DHCP_BOOTREPLY || _dhcpUdpSocket.remotePort() != DHCP_SERVER_PORT) {
        _dhcpUdpSocket.flush();
        return 0;
    }
    const uint32_t id = ntohl(fixed.xid);
    if (memcmp(fixed.chaddr, _dhcpMacAddr, 6) || id < _dhcpInitialTransactionId || id > _dhcpTransactionId) {
        _dhcpUdpSocket.flush();
        return 0;
    }

    // pav2000 Читает и отбрасывает count байтов DHCP-пакета. Возвращает true при полном чтении.
    const auto skip = [this](int count) {
        uint8_t scratch[16];
        while (count > 0) {
            const int chunk = count > (int)sizeof(scratch) ? sizeof(scratch) : count;
            if (_dhcpUdpSocket.read(scratch, chunk) != chunk) return false;
            count -= chunk;
        }
        return true;
    };

    uint8_t type = 0, mask[4], gateway[4], dnsIp[4], server[4];
    memcpy(mask, _dhcpSubnetMask, 4);
    memcpy(gateway, _dhcpGatewayIp, 4);
    memcpy(dnsIp, _dhcpDnsServerIp, 4);
    memcpy(server, _dhcpDhcpServerIp, 4);
    uint32_t t1 = _dhcpT1, t2 = _dhcpT2, lease = _dhcpLeaseTime;
    bool leaseSeen = false;
    bool valid = skip(240 - sizeof(fixed));
    while (valid && _dhcpUdpSocket.available() > 0) {
        const int option = _dhcpUdpSocket.read();
        if (option == endOption) break;
        if (option == padOption) continue;
        if (option < 0) { valid = false; break; }
        const int length = _dhcpUdpSocket.read();
        if (length < 0 || length > _dhcpUdpSocket.available()) { valid = false; break; }

        uint8_t *dest = nullptr;
        int used = 4;
        switch (option) {
        case dhcpMessageType: dest = &type; used = 1; break;
        case subnetMask: dest = mask; break;
        case routersOnSubnet: dest = gateway; break;
        case dns: dest = dnsIp; break;
        case dhcpServerIdentifier:
            if (!(server[0] || server[1] || server[2] || server[3]) ||
                IPAddress(server) == _dhcpUdpSocket.remoteIP()) dest = server;
            break;
        case dhcpT1value: dest = (uint8_t*)&t1; break;
        case dhcpT2value: dest = (uint8_t*)&t2; break;
        case dhcpIPaddrLeaseTime: dest = (uint8_t*)&lease; break;
        default: break;
        }
        if (!dest) used = 0;
        if (length < used || (used && _dhcpUdpSocket.read(dest, used) != used) || !skip(length - used)) {
            valid = false;
            break;
        }
        if (option == dhcpT1value) t1 = ntohl(t1);
        if (option == dhcpT2value) t2 = ntohl(t2);
        if (option == dhcpIPaddrLeaseTime) { lease = ntohl(lease); leaseSeen = true; }
    }
    _dhcpUdpSocket.flush();
    if (!valid || !type) return 0;

    transactionId = id;
    memcpy(_dhcpLocalIp, fixed.yiaddr, 4);
    memcpy(_dhcpSubnetMask, mask, 4);
    memcpy(_dhcpGatewayIp, gateway, 4);
    memcpy(_dhcpDnsServerIp, dnsIp, 4);
    memcpy(_dhcpDhcpServerIp, server, 4);
    _dhcpT1 = t1;
    _dhcpT2 = t2;
    _dhcpLeaseTime = lease;
    if (leaseSeen) _renewInSec = lease;
    return type;
}


/*
    returns:
    0/DHCP_CHECK_NONE: nothing happened
    1/DHCP_CHECK_RENEW_FAIL: renew failed
    2/DHCP_CHECK_RENEW_OK: renew success
    3/DHCP_CHECK_REBIND_FAIL: rebind fail
    4/DHCP_CHECK_REBIND_OK: rebind success
*/
int DhcpClass::checkLease(){
    //this uses a signed / unsigned trick to deal with millis overflow
    unsigned long now = millis();
    signed long snow = (long)now;
    int rc=DHCP_CHECK_NONE;
    if (_lastCheck != 0){
        signed long factor;
        //calc how many ms past the timeout we are
        factor = snow - (long)_secTimeout;
        //if on or passed the timeout, reduce the counters
        if ( factor >= 0 ){
            //next timeout should be now plus 1000 ms minus parts of second in factor
            _secTimeout = snow + 1000 - factor % 1000;
            //how many seconds late are we, minimum 1
            factor = factor / 1000 +1;
            
            //reduce the counters by that mouch
            //if we can assume that the cycle time (factor) is fairly constant
            //and if the remainder is less than cycle time * 2 
            //do it early instead of late
            if(_renewInSec < factor*2 )
                _renewInSec = 0;
            else
                _renewInSec -= factor;
            
            if(_rebindInSec < factor*2 )
                _rebindInSec = 0;
            else
                _rebindInSec -= factor;
        }

        //if we have a lease but should renew, do it
        if (_dhcp_state == STATE_DHCP_LEASED && _renewInSec <=0){
            _dhcp_state = STATE_DHCP_REREQUEST;
            rc = 1 + request_DHCP_lease();
        }

        //if we have a lease or is renewing but should bind, do it
        if( (_dhcp_state == STATE_DHCP_LEASED || _dhcp_state == STATE_DHCP_START) && _rebindInSec <=0){
            //this should basically restart completely
            _dhcp_state = STATE_DHCP_START;
            reset_DHCP_lease();
            rc = 3 + request_DHCP_lease();
        }
    }
    else{
        _secTimeout = snow + 1000;
    }

    _lastCheck = now;
    return rc;
}

IPAddress DhcpClass::getLocalIp()
{
    return IPAddress(_dhcpLocalIp);
}

IPAddress DhcpClass::getSubnetMask()
{
    return IPAddress(_dhcpSubnetMask);
}

IPAddress DhcpClass::getGatewayIp()
{
    return IPAddress(_dhcpGatewayIp);
}

IPAddress DhcpClass::getDhcpServerIp()
{
    return IPAddress(_dhcpDhcpServerIp);
}

IPAddress DhcpClass::getDnsServerIp()
{
    return IPAddress(_dhcpDnsServerIp);
}

void DhcpClass::printByte(char * buf, uint8_t n ) {
  char *str = &buf[1];
  buf[0]='0';
  do {
    unsigned long m = n;
    n /= 16;
    char c = m - 16 * n;
    *str-- = c < 10 ? c + '0' : c + 'A' - 10;
  } while(n);
}
