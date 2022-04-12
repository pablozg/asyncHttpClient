#ifndef asyncHttpClient_h
#define asyncHttpClient_h "1.0.0"

   /***********************************************************************************
    Copyright (C) <2021>  <Pablo Z.>
    Based on asyncHTTPrequest by Bob Lemaire, IoTaWatt, Inc.
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.  
   
***********************************************************************************/
#include <Arduino.h>

#ifndef DEBUG_IOTA_PORT
#define DEBUG_IOTA_PORT Serial
#endif

#ifdef DEBUG_IOTA_HTTP
#define DEBUG_IOTA_HTTP_SET true
#else
#define DEBUG_IOTA_HTTP_SET false
#endif

#ifndef ESP32
#include <ESPAsyncTCP.h>
#define _seize 
#define _release 
#endif

#ifdef ESP32
#include <AsyncTCP.h>
#define _seize xSemaphoreTakeRecursive(threadLock,portMAX_DELAY)
#define _release xSemaphoreGiveRecursive(threadLock)
#endif

#include <pgmspace.h>

#define DEBUG_HTTP(format,...)  if(_debug){\
                                    DEBUG_IOTA_PORT.printf("Debug(%3ld): ", millis()-_requestStartTime);\
                                    DEBUG_IOTA_PORT.printf_P(PSTR(format),##__VA_ARGS__);}

#define DEFAULT_RX_TIMEOUT 6                    // Seconds for timeout
#define MAX_MESSAGE_SIZE 4999

#define HTTPCODE_CONNECTION_REFUSED  (-1)
#define HTTPCODE_SEND_HEADER_FAILED  (-2)
#define HTTPCODE_SEND_PAYLOAD_FAILED (-3)
#define HTTPCODE_NOT_CONNECTED       (-4)
#define HTTPCODE_CONNECTION_LOST     (-5)
#define HTTPCODE_NO_STREAM           (-6)
#define HTTPCODE_NO_HTTP_SERVER      (-7)
#define HTTPCODE_TOO_LESS_RAM        (-8)
#define HTTPCODE_ENCODING            (-9)
#define HTTPCODE_STREAM_WRITE        (-10)
#define HTTPCODE_TIMEOUT             (-11)


#define FOREACH_STATE(STATE) \
        STATE(readyStateUnsent) \
        STATE(readyStateRequest) \
        STATE(readyStateOpened) \
        STATE(readyStateHdrsRecvd) \
        STATE(readyStateLoading) \
        STATE(readyStateDone)  \

#define GENERATE_ENUM(ENUM) ENUM,
#define GENERATE_STRING(STRING) #STRING,

typedef enum {
    FOREACH_STATE(GENERATE_ENUM)
} reqStates;

static const char *reqStatesString[] __attribute__ ((unused)) = { FOREACH_STATE(GENERATE_STRING) };

// typedef enum
// {
//   readyStateUnsent      = 0,            // Client created, open not yet called
//   readyStateRequest     = 1,            // Client created, and open signal received but not yet connected
//   readyStateOpened      = 2,            // open() has been called, connected
//   readyStateHdrsRecvd   = 3,            // send() called, response headers available
//   readyStateLoading     = 4,            // receiving, partial data available
//   readyStateDone        = 5             // Request complete, all data available.
// } reqStates;

// static char reqStatesText[] = {"readyStateUnsent", "readyStateRequest", "readyStateOpened", "readyStateHdrsRecvd", "readyStateLoading","readyStateDone"};

class asyncHttpClient {

  struct header {
	  header*	 	next;
	  char*			name;
	  char*			value;
	  header():
        next(nullptr), 
        name(nullptr), 
        value(nullptr)
        {};
	  ~header()
    {
        delete[] name; 
        delete[] value; 
        delete next;
    }
  };

    typedef std::function<void(void*, asyncHttpClient*, int readyState)> readyStateChangeCB;
    typedef std::function<void(void*, asyncHttpClient*, size_t len)> onDataCB;
    typedef std::function<void(void*, asyncHttpClient*, uint32_t time)> onTimeoutCB;
    typedef std::function<void(void*, asyncHttpClient*)> onPollTimeoutCB;
    typedef std::function<void(void*, asyncHttpClient*, int8_t error)> onErrorCB;
	
  public:
    asyncHttpClient();
    ~asyncHttpClient();

     
    //External functions in typical order of use:
    //__________________________________________________________________________________________________________*/
    void    setDebug(bool);                                         // Turn debug message on/off
    bool    debug();                                                // is debug on or off?

    bool    open(const char* HOST, const int PORT, const char* request);        // Initiate a request
    void    onReadyStateChange(readyStateChangeCB, void* arg = 0);  // Optional event handler for ready state change
                                                                    // or you can simply poll readyState()    
    void	  setTimeout(int);                                        // overide default timeout (seconds)
    void    setReqHeader(const char* name, const char* value);      // add a request header 
    void    setReqHeader(const char* name, const __FlashStringHelper* value);
    void    setReqHeader(const __FlashStringHelper *name, const char* value);
    void    setReqHeader(const __FlashStringHelper *name, const __FlashStringHelper* value);

    void    setReqHeader(const char* name, int32_t value);          // overload to use integer value
    void    setReqHeader(const __FlashStringHelper *name, int32_t value);
        
    void    abort();                                                // Abort the current operation
    
    reqStates   readyState();                                       // Return the ready state
    const char* readyStateText();                                   // Return the ready state text

    int     respHeaderCount();                                      // Retrieve count of response headers
    char*   respHeaderName(int index);                              // Return header name by index
    char*   respHeaderValue(int index);                             // Return header value by index
    char*   respHeaderValue(const char* name);                      // Return header value by name
    char*   respHeaderValue(const __FlashStringHelper *name);
    bool    respHeaderExists(const char* name);                     // Does header exist by name?
    bool    respHeaderExists(const __FlashStringHelper *name);
    String  headers();                                              // Return all headers as String

    void    onData(onDataCB, void* arg = 0);                        // Notify when min data is available
    void    onTimeout(onTimeoutCB, void* arg = 0);                  // Notify on request timeout
    void    onPollTimeout(onPollTimeoutCB, void* arg = 0);          // Notify on poll timeout
    void    onError(onErrorCB, void* arg = 0);                      // Notify on error
    size_t  available();                                            // response available
    size_t  responseLength();                                       // indicated response length or sum of chunks to date     
    int     responseHTTPcode();                                     // HTTP response code or (negative) error code
    const char*   responseText();                                   // response (whole* or partial* as string)
    const char*   responseHost();                                   // response host ip as string
    uint32_t elapsedTime();                                         // Elapsed time of in progress transaction or last completed (ms)
    String  version();                                              // Version of asyncHttpClient
//___________________________________________________________________________________________________________________________________

  private:
  
    enum    {HTTPmethodGET,	HTTPmethodPOST} _HTTPmethod;
			
    reqStates       _readyState;

    int16_t         _HTTPcode;                  // HTTP response code or (negative) exception code
    bool            _chunked;                   // Processing chunked response
    bool            _debug;                     // Debug state
    uint32_t        _timeout;                   // Default or user overide RxTimeout in seconds
    uint32_t        _lastActivity;              // Time of last activity 
    uint32_t        _requestStartTime;          // Time last open() issued
    uint32_t        _requestEndTime;            // Time of last disconnect
    char*           _connectedHost;             // Host when connected
    int             _connectedPort;             // Port when connected
    AsyncClient*    _client;                    // ESPAsyncTCP AsyncClient instance
    size_t          _contentLength;             // content-length header value or sum of chunk headers  
    size_t          _contentRead;               // number of bytes retrieved by user since last open()
    readyStateChangeCB  _readyStateChangeCB;    // optional callback for readyState change
    void*           _readyStateChangeCBarg;     // associated user argument
    onDataCB        _onDataCB;                  // optional callback when data received
    void*           _onDataCBarg;               // associated user argument
    onTimeoutCB     _onTimeoutCB;               // optional callback when timeout received
    void*           _onTimeoutCBarg;            // associated user argument
    onPollTimeoutCB _onPollTimeoutCB;           // optional callback when poll timeout received
    void*           _onPollTimeoutCBarg;        // associated user argument
    onErrorCB       _onErrorCB;                 // optional callback when error received
    void*           _onErrorCBarg;              // associated user argument

    const char*     _rawURL;
    char*           _content;

    #ifdef ESP32
    SemaphoreHandle_t threadLock;
    #endif
    
    header*     _headers;                       // request or (readyState > readyStateHdrsRcvd) response headers    

    // Protected functions

    header*     _addHeader(const char*, const char*);
    header*     _getHeader(const char*);
    header*     _getHeader(int);

    void        _processConnection();
    bool        _connect();
    bool        _connect(const char* HOST, const int PORT);
    void        _send();
    void        _setReadyState(reqStates);
    char*       _charstar(const __FlashStringHelper *str);
    
    // callbacks

    void        _onConnect(AsyncClient*);
    void        _onDisconnect(AsyncClient*);
    void        _onData(void*, size_t);
    void        _onError(AsyncClient*, int8_t);
    void        _onPoll(AsyncClient*);
    void        _onTimeout(AsyncClient*, uint32_t);
    bool        _collectHeaders(char* buffer, size_t len);
};
#endif 