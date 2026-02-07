// service/http_hello.cpp - minimal HTTP server for GET /hello
//
// Provenance:
// - Uses Circle's CHTTPDaemon pattern (see Circle sample/21-webserver).

#include "http_hello.h"

#include <circle/string.h>
#include <circle/types.h>

#include <stdio.h>
#include <string.h>

static constexpr u16 kServicePort = 15410;
static constexpr unsigned kMaxContentSize = 1024;

CServiceHelloServer::CServiceHelloServer (CNetSubSystem *pNetSubSystem, CSocket *pSocket)
	: CHTTPDaemon(pNetSubSystem, pSocket, kMaxContentSize, kServicePort),
	  m_pNet(pNetSubSystem)
{
}

CServiceHelloServer::~CServiceHelloServer (void) = default;

CHTTPDaemon *CServiceHelloServer::CreateWorker (CNetSubSystem *pNetSubSystem, CSocket *pSocket)
{
	return new CServiceHelloServer(pNetSubSystem, pSocket);
}

THTTPStatus CServiceHelloServer::GetContent (const char  *pPath,
					     const char  *pParams,
					     const char  *pFormData,
					     u8	     *pBuffer,
					     unsigned    *pLength,
					     const char **ppContentType)
{
	(void) pParams;
	(void) pFormData;

	if (!pPath || !pBuffer || !pLength || !ppContentType)
	{
		return HTTPBadRequest;
	}

	if (strcmp(pPath, "/hello") != 0 && strcmp(pPath, "/hello/") != 0)
	{
		return HTTPNotFound;
	}

	CString ip;
	if (m_pNet)
	{
		m_pNet->GetConfig()->GetIPAddress()->Format(&ip);
	}

	*ppContentType = "application/json";

	const unsigned cap = *pLength;
	const int n = snprintf((char *) pBuffer, cap,
			       "{\"ok\":true,\"state\":\"READY\",\"ip\":\"%s\",\"port\":%u}\n",
			       ip.GetLength() ? (const char *) ip : "0.0.0.0",
			       (unsigned) kServicePort);
	if (n < 0 || (unsigned) n >= cap)
	{
		return HTTPInternalServerError;
	}

	*pLength = (unsigned) n;
	return HTTPOK;
}

