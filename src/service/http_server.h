// service/http_server.h - HTTP control plane for the service kernel.
//
// See docs/service-http.md for the API and on-SD layout contract.
//
// Uses Circle's CHTTPDaemon (vendor-patched for PUT + raw header/body access).

#ifndef NETSERVICE_HTTP_SERVER_H
#define NETSERVICE_HTTP_SERVER_H

#include <circle/net/httpdaemon.h>

class CServiceHttpServer : public CHTTPDaemon
{
public:
	explicit CServiceHttpServer(CNetSubSystem *pNetSubSystem, CSocket *pSocket = nullptr);
	~CServiceHttpServer(void) override;

	// Teardown is requested after a successful upload+commit so the service kernel
	// can stop serving and reboot back into the emulator kernel.
	static void RequestTeardown(void);
	static bool IsTeardownRequested(void);

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
