// service/http_hello.h - minimal HTTP server for GET /hello
//
// Provenance:
// - Uses Circle's CHTTPDaemon pattern (see Circle sample/21-webserver).

#ifndef NETSERVICE_HTTP_HELLO_H
#define NETSERVICE_HTTP_HELLO_H

#include <circle/net/httpdaemon.h>

class CServiceHelloServer : public CHTTPDaemon
{
public:
	CServiceHelloServer (CNetSubSystem *pNetSubSystem, CSocket *pSocket = 0);
	~CServiceHelloServer (void) override;

	CHTTPDaemon *CreateWorker (CNetSubSystem *pNetSubSystem, CSocket *pSocket) override;
	THTTPStatus GetContent (const char  *pPath,
				const char  *pParams,
				const char  *pFormData,
				u8	     *pBuffer,
				unsigned    *pLength,
				const char **ppContentType) override;

private:
	CNetSubSystem *m_pNet;
};

#endif

