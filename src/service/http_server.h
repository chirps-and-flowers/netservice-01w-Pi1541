// service/http_server.h - HTTP control plane for the service kernel
//
// Provenance:
// - Ported from pi1541_01w_network_old_works/src/meta_server.cpp (prototype).
// - Uses Circle's CHTTPDaemon (patched to support PUT + raw header/body access).

#ifndef NETSERVICE_HTTP_SERVER_H
#define NETSERVICE_HTTP_SERVER_H

#include <circle/net/httpdaemon.h>

class CServiceHttpServer : public CHTTPDaemon
{
public:
	explicit CServiceHttpServer(CNetSubSystem *pNetSubSystem, CSocket *pSocket = nullptr);
	~CServiceHttpServer(void) override;

	CHTTPDaemon *CreateWorker(CNetSubSystem *pNetSubSystem, CSocket *pSocket) override;

	THTTPStatus GetContent(const char *pPath,
			       const char *pParams,
			       const char *pFormData,
			       u8 *pBuffer,
			       unsigned *pLength,
			       const char **ppContentType) override;

private:
	THTTPStatus HandleUpload(u8 *pBuffer, unsigned *pLength, bool append_list, bool mark_complete);

	CNetSubSystem *m_pNetSubSystem;
};

#endif
