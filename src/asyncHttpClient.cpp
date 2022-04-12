#include "asyncHttpClient.h"

//**************************************************************************************************************
asyncHttpClient::asyncHttpClient()
    : _readyState(readyStateUnsent)
    , _HTTPcode(0)
    , _chunked(false)
    , _debug(DEBUG_IOTA_HTTP_SET)
    , _timeout(DEFAULT_RX_TIMEOUT)
    , _lastActivity(0)
    , _requestStartTime(0)
    , _requestEndTime(0)
    , _connectedHost(nullptr)
    , _connectedPort(-1)
    , _client(nullptr)
    , _contentLength(0)
    , _contentRead(0)
    , _readyStateChangeCB(nullptr)
    , _readyStateChangeCBarg(nullptr)
    , _onDataCB(nullptr)
    , _onDataCBarg(nullptr)
    , _onTimeoutCB(nullptr)
    , _onTimeoutCBarg(nullptr)
    , _onPollTimeoutCB(nullptr)
    , _onPollTimeoutCBarg(nullptr)
    , _onErrorCB(nullptr)
    , _onErrorCBarg(nullptr)
    , _rawURL(nullptr)
    , _content(nullptr)
    , _headers(nullptr)
{
    DEBUG_HTTP("New request.");
    _connectedHost = new char[80];
    _content = new char[MAX_MESSAGE_SIZE + 1];
#ifdef ESP32
    threadLock = xSemaphoreCreateRecursiveMutex();
#endif
}

//**************************************************************************************************************
asyncHttpClient::~asyncHttpClient(){
    if(_client) { 
        _client->abort(); 
        _client->close(true);
        delete _client;
    }
    delete _headers;
    delete[] _content;
    delete[] _connectedHost;
#ifdef ESP32
    vSemaphoreDelete(threadLock);
#endif
}

//**************************************************************************************************************
void    asyncHttpClient::setDebug(bool debug){
    DEBUG_HTTP("setDebug(%s) version %s\r\n", debug ? "on" : "off", asyncHttpClient_h);
    _debug = debug;
}

//**************************************************************************************************************
bool    asyncHttpClient::debug(){
    return(_debug);
}

//**************************************************************************************************************
bool	asyncHttpClient::open(const char* HOST, const int PORT, const char* request){
    DEBUG_HTTP("open(%s:%d)\r\n", HOST, PORT);

    if(_readyState != readyStateUnsent && _readyState != readyStateDone) {
        DEBUG_HTTP("open(request already made)\r\n");
        return false;
    }
    _seize;
    _requestStartTime = millis();
    delete _headers;
    _headers = nullptr;
    // delete[] _content;
    // _content = nullptr;
    _rawURL = request;
    _chunked = false;
    _contentRead = 0;
    _readyState = readyStateRequest;

    if( _client && _client->connected() && 
      (strcmp(HOST, _connectedHost) != 0 || PORT != _connectedPort)){ return false; }
    
    _lastActivity = millis();
    
    _release;
	return _connect(HOST, PORT);
}

//**************************************************************************************************************
void	asyncHttpClient::setTimeout(int seconds){
    DEBUG_HTTP("setTimeout(%d)\r\n", seconds);
    _timeout = seconds;
}

//**************************************************************************************************************
void    asyncHttpClient::abort(){
    DEBUG_HTTP("abort()\r\n");
    _seize;
    if(! _client) return;
    _client->abort();
    _release;
}

//**************************************************************************************************************
reqStates   asyncHttpClient::readyState(){
    return _readyState;
}

//**************************************************************************************************************
const char*   asyncHttpClient::readyStateText(){
    return reqStatesString[_readyState];
}

//**************************************************************************************************************
int	asyncHttpClient::responseHTTPcode(){
    return _HTTPcode;
}

//**************************************************************************************************************
const char*	asyncHttpClient::responseText(){
    // DEBUG_HTTP("responseText()\n");
    DEBUG_HTTP("responseText() %.40s... (%d)\r\n\n", _content, strlen(_content));
    _seize;

    if (strlen(_content) == 0) {
        // _release;
        // return '\0';
        _content[0] = '\0';
    }
    
    _release; 
    return _content;
}

const char*	asyncHttpClient::responseHost(){
    DEBUG_HTTP("responseHost() %s\r\n\n", _connectedHost);

    return _connectedHost;
}

//**************************************************************************************************************
size_t	asyncHttpClient::responseLength(){
    if(_readyState < readyStateLoading) return 0;
    return _contentLength > 0 ? _contentLength : _contentRead;
}

//**************************************************************************************************************
void    asyncHttpClient::onReadyStateChange(readyStateChangeCB cb, void* arg){
    _readyStateChangeCB = cb;
    _readyStateChangeCBarg = arg;
}

//**************************************************************************************************************
void	asyncHttpClient::onData(onDataCB cb, void* arg){
    DEBUG_HTTP("onData() CB set\r\n");
    _onDataCB = cb;
    _onDataCBarg = arg;
}

//**************************************************************************************************************
void	asyncHttpClient::onTimeout(onTimeoutCB cb, void* arg){
    DEBUG_HTTP("onTimeout() CB set\r\n");
    _onTimeoutCB = cb;
    _onTimeoutCBarg = arg;
}

//**************************************************************************************************************
void	asyncHttpClient::onPollTimeout(onPollTimeoutCB cb, void* arg){
    DEBUG_HTTP("onPollTimeout() CB set\r\n");
    _onPollTimeoutCB = cb;
    _onPollTimeoutCBarg = arg;
}

void	asyncHttpClient::onError(onErrorCB cb, void* arg){
    DEBUG_HTTP("onError() CB set\r\n");
    _onErrorCB = cb;
    _onErrorCBarg = arg;
}

//**************************************************************************************************************
uint32_t asyncHttpClient::elapsedTime(){
    if(_readyState <= readyStateOpened) return 0;
    if(_readyState != readyStateDone){
        return millis() - _requestStartTime;
    }
    return _requestEndTime - _requestStartTime;
}

//**************************************************************************************************************
String asyncHttpClient::version(){
    return String(asyncHttpClient_h);
}

/*______________________________________________________________________________________________________________

               PPPP    RRRR     OOO    TTTTT   EEEEE    CCC    TTTTT   EEEEE   DDDD
               P   P   R   R   O   O     T     E       C   C     T     E       D   D
               PPPP    RRRR    O   O     T     EEE     C         T     EEE     D   D
               P       R  R    O   O     T     E       C   C     T     E       D   D
               P       R   R    OOO      T     EEEEE    CCC      T     EEEEE   DDDD
_______________________________________________________________________________________________________________*/

//**************************************************************************************************************
bool  asyncHttpClient::_connect(const char* HOST, const int PORT){
    DEBUG_HTTP("_connect()\r\n");
    _seize;
    if( ! _client) {
        _client = new AsyncClient();
    }

    strcpy(_connectedHost, HOST);
    _connectedPort = PORT;
    _client->onConnect([](void *obj, AsyncClient *client){((asyncHttpClient*)(obj))->_onConnect(client);}, this);
    _client->onDisconnect([](void *obj, AsyncClient* client){((asyncHttpClient*)(obj))->_onDisconnect(client);}, this);
    _client->onPoll([](void *obj, AsyncClient *client){((asyncHttpClient*)(obj))->_onPoll(client);}, this);
    _client->onError([](void *obj, AsyncClient *client, uint32_t error){((asyncHttpClient*)(obj))->_onError(client, error);}, this);
    _client->onTimeout([](void *obj, AsyncClient *client, uint32_t time){((asyncHttpClient*)(obj))->_onTimeout(client, time);}, this);
    _client->setRxTimeout(DEFAULT_RX_TIMEOUT);
    if( ! _client->connected()){
        if( ! _client->connect(HOST, PORT)) {
            DEBUG_HTTP("!client.connect(%s, %d) failed\r\n", HOST, PORT);
            _HTTPcode = HTTPCODE_NOT_CONNECTED;
            _setReadyState(readyStateUnsent);
            return false;
        }
    }
    else {
        _onConnect(_client);
    }
    _lastActivity = millis();

    _release;
    return true;
}

//**************************************************************************************************************
void  asyncHttpClient::_send(){
    DEBUG_HTTP("_send()\r\n");
    
    if (!_client) return;
    
    if (!_client->connected() || ! _client->canSend()) {
        DEBUG_HTTP("*can't send\r\n");
        return;
    }
    
    _client->write(_rawURL);
    _lastActivity = millis(); 
}

//**************************************************************************************************************
void  asyncHttpClient::_setReadyState(reqStates newState){
    if(_readyState != newState){
        _readyState = newState;          
        DEBUG_HTTP("_setReadyState(%s)\r\n", reqStatesString[_readyState]);
        if(_readyStateChangeCB){
            _readyStateChangeCB(_readyStateChangeCBarg, this, _readyState);
        }
    } 
}

//**************************************************************************************************************
void  asyncHttpClient::_processConnection(){
    const char* connectionHdr = respHeaderValue("connection");
    
    if (_contentLength == 0 && _contentRead > 0) { _contentLength = strlen(_content); }
    
    _setReadyState(readyStateDone);
    
    if(connectionHdr && (strcasecmp_P(connectionHdr,PSTR("disconnect")) == 0 || strcasecmp_P(connectionHdr,PSTR("close")) == 0)){
        DEBUG_HTTP("*all data received - closing TCP\r\n");
        _client->close();
    }
    else {
        DEBUG_HTTP("*all data received - no disconnect\r\n");
    }
    _requestEndTime = millis();
    _lastActivity = 0;
}

/*______________________________________________________________________________________________________________

EEEEE   V   V   EEEEE   N   N   TTTTT         H   H    AAA    N   N   DDDD    L       EEEEE   RRRR     SSS
E       V   V   E       NN  N     T           H   H   A   A   NN  N   D   D   L       E       R   R   S 
EEE     V   V   EEE     N N N     T           HHHHH   AAAAA   N N N   D   D   L       EEE     RRRR     SSS
E        V V    E       N  NN     T           H   H   A   A   N  NN   D   D   L       E       R  R        S
EEEEE     V     EEEEE   N   N     T           H   H   A   A   N   N   DDDD    LLLLL   EEEEE   R   R    SSS 
_______________________________________________________________________________________________________________*/

//**************************************************************************************************************
void  asyncHttpClient::_onConnect(AsyncClient* client){
    DEBUG_HTTP("_onConnect handler\r\n");
    _seize;
    _client = client;
    _setReadyState(readyStateOpened);

    memset(_content, 0, MAX_MESSAGE_SIZE + 1);
    _contentLength = 0;
    _contentRead = 0;
    _chunked = false;
    //_client->onAck([](void* obj, AsyncClient* client, size_t len, uint32_t time){((asyncHttpClient*)(obj))->_send();}, this);
    _client->onData([](void* obj, AsyncClient* client, void* data, size_t len) { ((asyncHttpClient*)(obj))->_onData(data, len); }, this);
    if(client->space() > 32 && _client->canSend()) {
        _send();
    }
    _lastActivity = millis();
    _release;
}

//**************************************************************************************************************
void  asyncHttpClient::_onPoll(AsyncClient* client){
    DEBUG_HTTP("_onPoll handler\r\n");
    _seize;
    if(_timeout && (millis() - _lastActivity) > (_timeout * 1000)){
        DEBUG_HTTP("_onPoll timeout\r\n");
        
        if(_onPollTimeoutCB){
            _onPollTimeoutCB(_onPollTimeoutCBarg, this);
        }

        _HTTPcode = HTTPCODE_TIMEOUT;
        _setReadyState(readyStateUnsent);
    }
    _release;     
}

//**************************************************************************************************************
void  asyncHttpClient::_onError(AsyncClient* client, int8_t error){
    DEBUG_HTTP("_onError handler error=%s\r\n", _client->errorToString(error));
    _seize;
    if(_onErrorCB){
        _onErrorCB(_onErrorCBarg, this, error);
    }

    if (_client) { _client->close(); }
    _HTTPcode = error;
    _setReadyState(readyStateUnsent);
    _release; 
}

//**************************************************************************************************************
void  asyncHttpClient::_onTimeout(AsyncClient* client, uint32_t time){
    DEBUG_HTTP("_onTimeout handler\r\n");
    _seize;
    if (_client) { _client->close(); }
    _HTTPcode = HTTPCODE_TIMEOUT;
    _setReadyState(readyStateUnsent);

    if(_onTimeoutCB){
        _onTimeoutCB(_onTimeoutCBarg, this, time);
    }
    _release; 
}

//**************************************************************************************************************
void  asyncHttpClient::_onDisconnect(AsyncClient* client){
    DEBUG_HTTP("_onDisconnect handler\r\n");
    _seize;
    
    if(_readyState < readyStateOpened){
        _HTTPcode = HTTPCODE_NOT_CONNECTED;
    }
    else if (_HTTPcode > 0 && 
            (_readyState < readyStateHdrsRecvd || _contentRead < _contentLength)) {
        _HTTPcode = HTTPCODE_CONNECTION_LOST;
    }

    if (_contentRead > 0) {
        _setReadyState(readyStateDone);
    } else {
        _setReadyState(readyStateUnsent);
    }
    
    if (_client) {
        _client->abort();
        delete _client;
        _client = nullptr;
        _connectedPort = -1;
    }
    
    _requestEndTime = millis();
    _lastActivity = 0;
    
    _release;
}

//**************************************************************************************************************
void  asyncHttpClient::_onData(void* data, size_t len){
    DEBUG_HTTP("1- _onData handler %.16s... (%d)\r\n",(char*) data, len);
    // DEBUG_HTTP("1- _onData handler %s... (%d)\r\n",(char*) data, len); // comment in production
    _seize;
    _lastActivity = millis();

    char *tempData = (char *)data;
    char *dataPointer;
    uint16_t dataLength = 0;
    uint16_t dataPos = 0;
    bool chunkDataLen = true;
    bool chunkDataReceived = false;

    if(_readyState == readyStateOpened) {

        dataPointer = strstr(tempData, "\r\n\r\n");
        if ( dataPointer != NULL )
        { 
            dataPos = dataPointer - tempData + 4;
            dataLength = len - dataPos;

            // Add end char for split headers
            strncpy (dataPointer, "\0", 1);
        }

        // DEBUG_HTTP("_onData handler - Headers: %s... (%d)\r\n", tempData, strlen(tempData)); // comment in production
        // DEBUG_HTTP("2- _onData handler - Headers: %s... (%d), dataPos: %d, dataLength: %d\r\n", tempData, strlen(tempData), dataPos, dataLength); // comment in production

        if( ! _collectHeaders(tempData, len)) return;

        strncpy(_content, tempData + dataPos, dataLength);
        _content[dataLength] = '\0';
        _contentRead = strlen(_content);
        DEBUG_HTTP("3- _onData handler - Data: %.16s... (%d)\r\n", _content, strlen(_content));
        // DEBUG_HTTP("3- _onData handler - Data: %s... (%d)\r\n", _content, strlen(_content)); // comment in production
        // DEBUG_HTTP("3- _onData handler - Data: %s... (%d), dataPos: %d, dataLength: %d, _contentLength: %d\r\n", _content, strlen(_content), dataPos, dataLength, _contentLength); // comment in production
    }   
    
    // If there's data in the buffer and not Done,
    // advance readyState to Loading.

    if (_readyState != readyStateDone) {
        if(_readyState == readyStateLoading) {

            // Process data chunks
            if(!_chunked) {
                dataPos = strlen(_content);
                for (int i = 0; i < len; i++){
                    _content[i + dataPos] = tempData[i];
                }
                _content[len + dataPos] = '\0';
                _contentRead += len;
            } else { // Process data chunks in "transfer-encoding: chunked"

                dataPointer = strtok (tempData,"\r\n");
                while (dataPointer != NULL)
                {
                    if (chunkDataLen) {
                        chunkDataLen = false;
                        dataLength = strtol(dataPointer, NULL, 16);
                        DEBUG_HTTP("chunks data len: %d\n", dataLength);                        
                        if (dataLength == 0) { chunkDataReceived = true; break;}
                    } else {
                        for (int i = 0; i < dataLength; i++){
                            _content[i + _contentRead] = dataPointer[i];
                        }
                        _content[dataLength + _contentRead] = '\0';
                        _contentRead += dataLength;
                        DEBUG_HTTP("chunks data text: %s\n", dataPointer);
                        chunkDataLen = true;
                    }                    
                    dataPointer = strtok (NULL, "\r\n");
                }
            }
            
            DEBUG_HTTP("4- _onData handler - _contentRead: %d _contentLength: %d _contentSize: %d\r\n", _contentRead, _contentLength, strlen(_content));
            
            if (_chunked && chunkDataReceived) { _processConnection(); }

        } else { _setReadyState(readyStateLoading); }
    }

    // If not chunked and all data read, close it up.

    // if( !_chunked && strlen(_content) == _contentLength) {
    if( !_chunked && strlen(_content) == _contentLength && _contentLength != 0) {
        _processConnection();
    }

    // If onData callback requested, do so.

    if(_onDataCB){
        _onDataCB(_onDataCBarg, this, len);
    }
    
    _release;                  
}

//**************************************************************************************************************
bool  asyncHttpClient::_collectHeaders(char* buffer, size_t len){
    DEBUG_HTTP("_collectHeaders()\r\n");
    char *pch;
    char *pch2;
    char value[50];
    char name[50];
    uint16_t pos;

    // Loop to parse off each header line.
    // Drop out and return false if no \r\n (incomplete)

    if (_readyState != readyStateHdrsRecvd) {
        if (strncmp(buffer, "HTTP/1.", 7) == 0)
        {
            strncpy(value , buffer + 9, 3);
             _HTTPcode = atoi(value);
            //  DEBUG_HTTP("_collectHeaders() HTTP Code: %d\r\n", _HTTPcode);
        }
       
        pch = strtok (buffer,"\r\n");
        while (pch != NULL)
        {
            // DEBUG_HTTP("_collectHeaders() pch: %s len: %d\n", pch, pchLen);
            pch2 = strchr(pch, ':');
            if (pch2 != NULL) {
                pos = int(pch2-pch+1);
                 
                // DEBUG_HTTP("_collectHeaders() name -> pch: %s, pch2: %s, pos: %d\n", pch, pch2, pos);
                for (int i = 0; i < pos; i++) { name[i] = pch[i]; }
                name[pos - 1] = '\0';
                
                // DEBUG_HTTP("_collectHeaders() value -> pch: %s, pch2: %s, pos: %d\n", pch, pch2, pos);
                for (int i = 0; i < strlen(pch2); i++) { value[i] = pch2[i + 2];}
                value[strlen(pch2)] = '\0';

                _addHeader(name, value);
                // DEBUG_HTTP("_collectHeaders() Name: %s - value: %s\n", name, value);
            }
            
            pch = strtok (NULL, "\r\n");
        }

         _setReadyState(readyStateHdrsRecvd);
    }

    // If content-Length header, set _contentLength

    header *hdr = _getHeader("Content-Length");
    if(hdr){
        _contentLength = strtol(hdr->value, nullptr, 10);
    }

    // If chunked specified, try to set _contentLength to size of first chunk

    hdr = _getHeader("Transfer-Encoding"); 
    if(hdr && strcasecmp_P(hdr->value, PSTR("chunked")) == 0){
        DEBUG_HTTP("*transfer-encoding: chunked\r\n");
        _chunked = true;
        _contentLength = 0;
    }         

    return true;
}
        
// /*_____________________________________________________________________________________________________________

//                         H   H  EEEEE   AAA   DDDD   EEEEE  RRRR    SSS
//                         H   H  E      A   A  D   D  E      R   R  S   
//                         HHHHH  EEE    AAAAA  D   D  EEE    RRRR    SSS
//                         H   H  E      A   A  D   D  E      R  R       S
//                         H   H  EEEEE  A   A  DDDD   EEEEE  R   R   SSS
// ______________________________________________________________________________________________________________*/

//**************************************************************************************************************
void	asyncHttpClient::setReqHeader(const char* name, const char* value){
    if(_readyState <= readyStateOpened && _headers){
        _addHeader(name, value);
    }
}

//**************************************************************************************************************
void	asyncHttpClient::setReqHeader(const char* name, const __FlashStringHelper* value){
    if(_readyState <= readyStateOpened && _headers){
        char* _value = _charstar(value);
        _addHeader(name, _value);
        delete[] _value;
    }
}

//**************************************************************************************************************
void	asyncHttpClient::setReqHeader(const __FlashStringHelper *name, const char* value){
    if(_readyState <= readyStateOpened && _headers){
        char* _name = _charstar(name);
        _addHeader(_name, value);
        delete[] _name;
    }
}

//**************************************************************************************************************
void	asyncHttpClient::setReqHeader(const __FlashStringHelper *name, const __FlashStringHelper* value){
    if(_readyState <= readyStateOpened && _headers){
        char* _name = _charstar(name);
        char* _value = _charstar(value);
        _addHeader(_name, _value);
        delete[] _name;
        delete[] _value;
    }
}

//**************************************************************************************************************
void	asyncHttpClient::setReqHeader(const char* name, int32_t value){
    if(_readyState <= readyStateOpened && _headers){
        setReqHeader(name, String(value).c_str());
    }
}

//**************************************************************************************************************
void	asyncHttpClient::setReqHeader(const __FlashStringHelper *name, int32_t value){
    if(_readyState <= readyStateOpened && _headers){
        char* _name = _charstar(name);
        setReqHeader(_name, String(value).c_str());
        delete[] _name;
    }
}

//**************************************************************************************************************
int		asyncHttpClient::respHeaderCount(){
    if(_readyState < readyStateHdrsRecvd) return 0;                                            
    int count = 0;
    header* hdr = _headers;
    while(hdr){
        count++;
        hdr = hdr->next;
    }
    return count;
}

//**************************************************************************************************************
char*   asyncHttpClient::respHeaderName(int ndx){
    if(_readyState < readyStateHdrsRecvd) return nullptr;      
    header* hdr = _getHeader(ndx);
    if ( ! hdr) return nullptr;
    return hdr->name;
}

//**************************************************************************************************************
char*   asyncHttpClient::respHeaderValue(const char* name){
    if(_readyState < readyStateHdrsRecvd) return nullptr;      
    header* hdr = _getHeader(name);
    if( ! hdr) return nullptr;
    return hdr->value;
}

//**************************************************************************************************************
char*   asyncHttpClient::respHeaderValue(const __FlashStringHelper *name){
    if(_readyState < readyStateHdrsRecvd) return nullptr;
    char* _name = _charstar(name);      
    header* hdr = _getHeader(_name);
    delete[] _name;
    if( ! hdr) return nullptr;
    return hdr->value;
}

//**************************************************************************************************************
char*   asyncHttpClient::respHeaderValue(int ndx){
    if(_readyState < readyStateHdrsRecvd) return nullptr;      
    header* hdr = _getHeader(ndx);
    if ( ! hdr) return nullptr;
    return hdr->value;
}

//**************************************************************************************************************
bool	asyncHttpClient::respHeaderExists(const char* name){
    if(_readyState < readyStateHdrsRecvd) return false;      
    header* hdr = _getHeader(name);
    if ( ! hdr) return false;
    return true;
}

//**************************************************************************************************************
bool	asyncHttpClient::respHeaderExists(const __FlashStringHelper *name){
    if(_readyState < readyStateHdrsRecvd) return false;
    char* _name = _charstar(name);      
    header* hdr = _getHeader(_name);
    delete[] _name;
    if ( ! hdr) return false;
    return true;
}

//**************************************************************************************************************
String  asyncHttpClient::headers(){
    _seize;
    String _response = "";
    header* hdr = _headers;
    while(hdr){
        _response += hdr->name;
        _response += ':';
        _response += hdr->value;
        _response += "\r\n";
        hdr = hdr->next;
    }
    _response += "\r\n";
    _release;
    return _response;
}

//**************************************************************************************************************
asyncHttpClient::header*  asyncHttpClient::_addHeader(const char* name, const char* value){
    DEBUG_HTTP("_addHeader() Name: %s, value: %s\n", name, value);
    _seize;
    header* hdr = (header*) &_headers;
    while(hdr->next) {
        if(strcasecmp(name, hdr->next->name) == 0){
            header* oldHdr = hdr->next;
            hdr->next = hdr->next->next;
            oldHdr->next = nullptr;
            delete oldHdr;
        }
        else {
            hdr = hdr->next;
        }
    }
    hdr->next = new header;
    hdr->next->name = new char[strlen(name)+1];
    strcpy(hdr->next->name, name);
    hdr->next->value = new char[strlen(value)+1];
    strcpy(hdr->next->value, value);
    _release;
    return hdr->next;
}

//**************************************************************************************************************
asyncHttpClient::header* asyncHttpClient::_getHeader(const char* name){
    _seize;
    header* hdr = _headers;
    while (hdr) {
        if(strcasecmp(name, hdr->name) == 0) break;
        hdr = hdr->next;
    }
    _release;
    return hdr;
}

//**************************************************************************************************************
asyncHttpClient::header* asyncHttpClient::_getHeader(int ndx){
    _seize;
    header* hdr = _headers;
    while (hdr) {
        if( ! ndx--) break;
        hdr = hdr->next; 
    }
    _release;
    return hdr;
}

//**************************************************************************************************************
char* asyncHttpClient::_charstar(const __FlashStringHelper * str){
  if( ! str) return nullptr;
  char* ptr = new char[strlen_P((PGM_P)str)+1];
  strcpy_P(ptr, (PGM_P)str);
  return ptr;
}