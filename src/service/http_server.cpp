// service/http_server.cpp - HTTP control plane for the service kernel
//
// Provenance:
// - Ported from pi1541_01w_network_old_works/src/meta_server.cpp (prototype).
// - Uses Circle's CHTTPDaemon (patched to support PUT + raw header/body access).

#include "http_server.h"

#include <circle/bcmrandom.h>
#include <circle/net/http.h>
#include <circle/net/in.h>
#include <circle/string.h>
#include <circle/types.h>

#include <fatfs/ff.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static constexpr u16 kServicePort = 15410;
static constexpr unsigned kMaxContentSize = 8 * 1024 * 1024;

static const char kMetaContentType[] = "application/json";

static const char kIncomingDir[] = "/1541/_incoming";
static const char kActiveMountDir[] = "/1541/_active_mount";
static const char kTempDirtyDir[] = "/1541/_temp_dirty_disks";
static const char kActiveListPath[] = "/1541/_active_mount/ACTIVE.LST";
static const char kActiveListTmpPath[] = "/1541/_active_mount/ACTIVE.LST.tmp";

static const unsigned kPendingMax = 32;
static char g_pending_names[kPendingMax][64];
static unsigned g_pending_count = 0;
static uint32_t g_pending_nonce = 0;

static uint32_t g_nonce = 0;

static uint32_t MakeNonce(void)
{
	CBcmRandomNumberGenerator rng;
	return rng.GetNumber();
}

static bool ParseU32(const char *s, uint32_t *out)
{
	if (!out)
		return false;
	*out = 0;
	if (!s || !s[0])
		return false;

	char *end = nullptr;
	unsigned long v = strtoul(s, &end, 10);
	if (end == s)
		return false;
	*out = static_cast<uint32_t>(v);
	return true;
}

static bool EnsureDir(const char *path)
{
	if (!path || !path[0])
		return false;
	FILINFO fi;
	if (f_stat(path, &fi) == FR_OK)
		return (fi.fattrib & AM_DIR) != 0;
	return f_mkdir(path) == FR_OK;
}

static bool EnsureMetaDirs(void)
{
	return EnsureDir("/1541") && EnsureDir(kIncomingDir) && EnsureDir(kActiveMountDir) && EnsureDir(kTempDirtyDir);
}

static void TrimLine(char *s)
{
	if (!s)
		return;

	// Trim trailing whitespace.
	size_t n = strlen(s);
	while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n'))
		s[--n] = '\0';

	// Trim leading whitespace.
	size_t i = 0;
	while (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')
		++i;
	if (i)
		memmove(s, s + i, strlen(s + i) + 1);
}

static bool ReadActiveList(char out_names[kPendingMax][64], unsigned &out_count)
{
	out_count = 0;
	for (unsigned i = 0; i < kPendingMax; ++i)
	{
		out_names[i][0] = '\0';
	}

	FIL fp;
	if (f_open(&fp, kActiveListPath, FA_READ) != FR_OK)
		return false;

	char line[256];
	unsigned pos = 0;
	UINT br = 0;
	char ch = 0;
	while (f_read(&fp, &ch, 1, &br) == FR_OK && br == 1)
	{
		if (ch == '\r')
			continue;
		if (ch == '\n')
		{
			line[pos] = '\0';
			TrimLine(line);
			pos = 0;
			if (!line[0] || out_count >= kPendingMax)
				continue;
			snprintf(out_names[out_count], 64, "%s", line);
			++out_count;
			continue;
		}
		if (pos + 1 < sizeof(line))
			line[pos++] = ch;
	}

	// Handle last line without newline.
	if (pos && out_count < kPendingMax)
	{
		line[pos] = '\0';
		TrimLine(line);
		if (line[0])
		{
			snprintf(out_names[out_count], 64, "%s", line);
			++out_count;
		}
	}

	f_close(&fp);
	return out_count != 0;
}

static void SanitizeFilename(const char *input, char *output, size_t output_len)
{
	if (!output || output_len == 0)
		return;
	output[0] = '\0';
	if (!input)
		return;

	const char *base = input;
	for (const char *p = input; *p; ++p)
	{
		if (*p == '/' || *p == '\\')
			base = p + 1;
	}

	size_t written = 0;
	for (const char *p = base; *p && written + 1 < output_len; ++p)
	{
		unsigned char c = static_cast<unsigned char>(*p);
		if (c < 32 || c == 127)
			continue;
		if (c == '"' || c == '\'' || c == '<' || c == '>' || c == ':' || c == '|' || c == '?' || c == '*')
			continue;
		if (c == ';')
			continue;

		if (isalnum(c) || c == '.' || c == '_' || c == '-' || c == ' ')
		{
			output[written++] = static_cast<char>(c);
		}
		else
		{
			output[written++] = '_';
		}
	}
	output[written] = '\0';

	// Collapse spaces and trim.
	while (written > 0 && output[written - 1] == ' ')
		output[--written] = '\0';
	while (output[0] == ' ')
		memmove(output, output + 1, strlen(output));
	if (!output[0])
		snprintf(output, output_len, "upload");
}

static void EnsureExtension(char *name, size_t name_len, const char *type_hint)
{
	if (!name || name_len == 0)
		return;

	// If we already have an extension, keep it.
	if (strrchr(name, '.') != nullptr)
		return;

	const char *ext = nullptr;
	if (type_hint && type_hint[0])
	{
		ext = type_hint;
		if (ext[0] != '.')
		{
			// Allow "d64" and "D64" etc.
			static char dot_ext[16];
			snprintf(dot_ext, sizeof(dot_ext), ".%s", ext);
			ext = dot_ext;
		}
	}

	if (!ext || !ext[0])
		return;

	strncat(name, ext, name_len - strlen(name) - 1);
}

static uint32_t Crc32Update(uint32_t crc, const u8 *data, size_t len)
{
	static uint32_t table[256];
	static bool have_table = false;
	if (!have_table)
	{
		for (unsigned i = 0; i < 256; ++i)
		{
			uint32_t r = i;
			for (unsigned j = 0; j < 8; ++j)
				r = (r & 1) ? (r >> 1) ^ 0xEDB88320U : (r >> 1);
			table[i] = r;
		}
		have_table = true;
	}

	for (size_t i = 0; i < len; ++i)
		crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
	return crc;
}

static void ResetPendingUploads(uint32_t nonce, bool force)
{
	if (force || g_pending_nonce != nonce)
	{
		g_pending_nonce = nonce;
		g_pending_count = 0;
	}
}

static bool AddPendingUpload(const char *name)
{
	if (!name || !name[0])
		return false;
	if (g_pending_count >= kPendingMax)
		return false;
	snprintf(g_pending_names[g_pending_count], sizeof(g_pending_names[g_pending_count]), "%s", name);
	++g_pending_count;
	return true;
}

static bool ClearActiveMountDir(void)
{
	if (!EnsureMetaDirs())
		return false;

	DIR dir;
	FRESULT fr = f_opendir(&dir, kActiveMountDir);
	if (fr != FR_OK)
		return false;

	FILINFO fi;
	for (;;)
	{
		fr = f_readdir(&dir, &fi);
		if (fr != FR_OK)
		{
			f_closedir(&dir);
			return false;
		}
		if (fi.fname[0] == '\0')
			break;
		if (fi.fattrib & AM_DIR)
			continue;
		char path[256];
		snprintf(path, sizeof(path), "%s/%s", kActiveMountDir, fi.fname);
		f_unlink(path);
	}
	f_closedir(&dir);
	return true;
}

static bool WriteActiveListFromPending(void)
{
	if (!EnsureMetaDirs())
		return false;

	FIL fp;
	FRESULT fr = f_open(&fp, kActiveListTmpPath, FA_CREATE_ALWAYS | FA_WRITE);
	if (fr != FR_OK)
		return false;

	UINT written = 0;
	for (unsigned i = 0; i < g_pending_count; ++i)
	{
		const char *entry = g_pending_names[i];
		if (!entry || !entry[0])
			continue;
		f_write(&fp, entry, static_cast<UINT>(strlen(entry)), &written);
		const char nl = '\n';
		f_write(&fp, &nl, 1, &written);
	}
	f_sync(&fp);
	f_close(&fp);

	f_unlink(kActiveListPath);
	return f_rename(kActiveListTmpPath, kActiveListPath) == FR_OK;
}

static bool CommitPendingUploads(void)
{
	if (g_pending_count == 0)
		return false;
	if (!ClearActiveMountDir())
		return false;

	for (unsigned i = 0; i < g_pending_count; ++i)
	{
		const char *entry = g_pending_names[i];
		if (!entry || !entry[0])
			return false;
		char incoming_path[256];
		char active_path[256];
		snprintf(incoming_path, sizeof(incoming_path), "%s/%s", kIncomingDir, entry);
		snprintf(active_path, sizeof(active_path), "%s/%s", kActiveMountDir, entry);
		f_unlink(active_path);
		if (f_rename(incoming_path, active_path) != FR_OK)
			return false;
	}

	if (!WriteActiveListFromPending())
		return false;

	g_pending_count = 0;
	return true;
}

static THTTPStatus WriteJsonResult(u8 *pBuffer, unsigned *pLength, const char *result)
{
	if (!pBuffer || !pLength || !result)
		return HTTPInternalServerError;
	int written = snprintf(reinterpret_cast<char *>(pBuffer), *pLength, "%s", result);
	if (written < 0 || static_cast<unsigned>(written) >= *pLength)
	{
		*pLength = 0;
		return HTTPInternalServerError;
	}
	*pLength = static_cast<unsigned>(written);
	return HTTPOK;
}

static THTTPStatus WriteJsonError(u8 *pBuffer, unsigned *pLength, const char *error)
{
	char temp[256];
	snprintf(temp, sizeof(temp), "{\"ok\":false,\"error\":\"%s\"}", error ? error : "ERROR");
	return WriteJsonResult(pBuffer, pLength, temp);
}

THTTPStatus CServiceHttpServer::HandleUpload(u8 *pBuffer, unsigned *pLength, bool append_list, bool mark_complete)
{
	const char *nonce_text = GetHeaderValue("X-Nonce");
	uint32_t nonce = 0;
	if (!ParseU32(nonce_text, &nonce) || nonce != g_nonce)
		return WriteJsonError(pBuffer, pLength, "BAD_NONCE");

	unsigned body_len = 0;
	const u8 *body = GetRawRequestBody(&body_len);
	if (!body || body_len == 0)
		return WriteJsonError(pBuffer, pLength, "NO_BODY");

	const char *size_text = GetHeaderValue("X-Image-Size");
	uint32_t expected_size = 0;
	if (!size_text || !size_text[0])
		return WriteJsonError(pBuffer, pLength, "NO_SIZE");
	if (!ParseU32(size_text, &expected_size) || expected_size != body_len)
		return WriteJsonError(pBuffer, pLength, "BAD_SIZE");

	const char *crc_text = GetHeaderValue("X-CRC32");
	uint32_t expected_crc = 0;
	if (!crc_text || !crc_text[0])
		return WriteJsonError(pBuffer, pLength, "NO_CRC");
	if (!ParseU32(crc_text, &expected_crc))
		return WriteJsonError(pBuffer, pLength, "BAD_CRC");

	char orig_name[64];
	SanitizeFilename(GetHeaderValue("X-Image-Name"), orig_name, sizeof(orig_name));

	const char *type_hint = GetHeaderValue("X-Image-Type");

	char name[64];
	if (!append_list)
	{
		snprintf(name, sizeof(name), "ACTIVE");
		if ((!type_hint || !type_hint[0]) && orig_name[0])
		{
			const char *ext = strrchr(orig_name, '.');
			if (ext && ext[1])
				type_hint = ext;
		}
	}
	else
	{
		if (orig_name[0])
			snprintf(name, sizeof(name), "%s", orig_name);
		else
			snprintf(name, sizeof(name), "upload");
	}
	EnsureExtension(name, sizeof(name), type_hint);

	ResetPendingUploads(nonce, !append_list);
	if (!EnsureMetaDirs())
		return WriteJsonError(pBuffer, pLength, "FS_DIR");

	char temp_path[256];
	char final_path[256];
	snprintf(temp_path, sizeof(temp_path), "%s/%s.tmp", kIncomingDir, name);
	snprintf(final_path, sizeof(final_path), "%s/%s", kIncomingDir, name);

	FIL fp;
	FRESULT fr = f_open(&fp, temp_path, FA_CREATE_ALWAYS | FA_WRITE);
	if (fr != FR_OK)
		return WriteJsonError(pBuffer, pLength, "FS_OPEN");

	uint32_t crc = 0xFFFFFFFFU;
	UINT written = 0;
	const unsigned chunk = 4096;
	unsigned offset = 0;
	while (offset < body_len)
	{
		unsigned left = body_len - offset;
		unsigned take = left > chunk ? chunk : left;
		fr = f_write(&fp, body + offset, take, &written);
		if (fr != FR_OK || written != take)
		{
			f_close(&fp);
			f_unlink(temp_path);
			return WriteJsonError(pBuffer, pLength, "FS_WRITE");
		}
		crc = Crc32Update(crc, body + offset, take);
		offset += take;
	}
	crc ^= 0xFFFFFFFFU;
	f_sync(&fp);
	f_close(&fp);

	if (crc != expected_crc)
	{
		f_unlink(temp_path);
		return WriteJsonError(pBuffer, pLength, "BAD_CRC");
	}

	f_unlink(final_path);
	if (f_rename(temp_path, final_path) != FR_OK)
	{
		f_unlink(temp_path);
		return WriteJsonError(pBuffer, pLength, "FS_RENAME");
	}

	if (!AddPendingUpload(name))
	{
		f_unlink(final_path);
		return WriteJsonError(pBuffer, pLength, "QUEUE_FULL");
	}

	if (mark_complete)
	{
		if (!CommitPendingUploads())
			return WriteJsonError(pBuffer, pLength, "FS_COMMIT");
	}

	char response[256];
	snprintf(response, sizeof(response),
		 "{\"ok\":true,\"name\":\"%s\",\"size\":%u,\"crc32\":\"%08x\"}",
		 name, body_len, static_cast<unsigned>(crc));
	return WriteJsonResult(pBuffer, pLength, response);
}

CServiceHttpServer::CServiceHttpServer(CNetSubSystem *pNetSubSystem, CSocket *pSocket)
	: CHTTPDaemon(pNetSubSystem, pSocket, kMaxContentSize, kServicePort, 0),
	  m_pNetSubSystem(pNetSubSystem)
{
	if (g_nonce == 0)
	{
		g_nonce = MakeNonce();
	}
}

CServiceHttpServer::~CServiceHttpServer(void) = default;

CHTTPDaemon *CServiceHttpServer::CreateWorker(CNetSubSystem *pNetSubSystem, CSocket *pSocket)
{
	return new CServiceHttpServer(pNetSubSystem, pSocket);
}

THTTPStatus CServiceHttpServer::GetContent(const char *pPath,
					   const char *pParams,
					   const char *pFormData,
					   u8 *pBuffer,
					   unsigned *pLength,
					   const char **ppContentType)
{
	(void) pParams;
	(void) pFormData;

	if (!pPath || !pBuffer || !pLength || !ppContentType)
		return HTTPBadRequest;

	*ppContentType = kMetaContentType;

	THTTPRequestMethod method = GetRequestMethod();

	if (strcmp(pPath, "/hello") == 0 || strcmp(pPath, "/hello/") == 0)
	{
		if (method != HTTPRequestMethodGet)
			return HTTPMethodNotImplemented;

		// Keep prototype response shape to minimize frontend churn.
		char response[256];
		snprintf(response, sizeof(response),
			 "{\"state\":\"READY\",\"nonce\":%u,\"tcp_port\":%u,\"caps\":[],\"modified_count\":0,\"modified_id\":0}",
			 static_cast<unsigned>(g_nonce),
			 static_cast<unsigned>(kServicePort));
		return WriteJsonResult(pBuffer, pLength, response);
	}

	if (strcmp(pPath, "/upload/active") == 0 || strcmp(pPath, "/upload/active/") == 0)
	{
		if (method != HTTPRequestMethodPut && method != HTTPRequestMethodPost)
			return HTTPMethodNotImplemented;
		return HandleUpload(pBuffer, pLength, false, true);
	}

	if (strcmp(pPath, "/upload/active/add") == 0 || strcmp(pPath, "/upload/active/add/") == 0)
	{
		if (method != HTTPRequestMethodPut && method != HTTPRequestMethodPost)
			return HTTPMethodNotImplemented;
		return HandleUpload(pBuffer, pLength, true, false);
	}

	if (strcmp(pPath, "/upload/active/commit") == 0 || strcmp(pPath, "/upload/active/commit/") == 0)
	{
		if (method != HTTPRequestMethodPost)
			return HTTPMethodNotImplemented;

		const char *nonce_text = GetHeaderValue("X-Nonce");
		uint32_t nonce = 0;
		if (!ParseU32(nonce_text, &nonce) || nonce != g_nonce)
			return WriteJsonError(pBuffer, pLength, "BAD_NONCE");

		if (g_pending_count == 0)
			return WriteJsonError(pBuffer, pLength, "NO_FILES");

		if (!CommitPendingUploads())
			return WriteJsonError(pBuffer, pLength, "FS_COMMIT");

		return WriteJsonResult(pBuffer, pLength, "{\"ok\":true,\"committed\":true}");
	}

	if (strcmp(pPath, "/active/list") == 0 || strcmp(pPath, "/active/list/") == 0)
	{
		if (method != HTTPRequestMethodGet)
			return HTTPMethodNotImplemented;

		static char names[kPendingMax][64];
		unsigned count = 0;
		if (!ReadActiveList(names, count) || count == 0)
			return WriteJsonResult(pBuffer, pLength, "{\"count\":0,\"files\":[]}");

		char response[2048];
		unsigned off = 0;
		off += snprintf(response + off, sizeof(response) - off, "{\"count\":%u,\"files\":[", count);
		for (unsigned i = 0; i < count; ++i)
		{
			if (i)
				off += snprintf(response + off, sizeof(response) - off, ",");
			off += snprintf(response + off, sizeof(response) - off,
					"{\"i\":%u,\"name\":\"%s\"}", i + 1, names[i]);
			if (off >= sizeof(response))
				break;
		}
		off += snprintf(response + off, sizeof(response) - off, "]}");
		return WriteJsonResult(pBuffer, pLength, response);
	}

	if (strncmp(pPath, "/active/download/", 17) == 0)
	{
		if (method != HTTPRequestMethodGet)
			return HTTPMethodNotImplemented;

		static char names[kPendingMax][64];
		unsigned count = 0;
		if (!ReadActiveList(names, count) || count == 0)
			return HTTPNotFound;

		const char *idx_text = pPath + 17;
		unsigned idx = static_cast<unsigned>(atoi(idx_text));
		if (idx == 0 || idx > count)
			return HTTPNotFound;

		char path[256];
		snprintf(path, sizeof(path), "%s/%s", kActiveMountDir, names[idx - 1]);
		FIL fp;
		if (f_open(&fp, path, FA_READ) != FR_OK)
			return HTTPNotFound;

		const unsigned cap = *pLength;
		const FSIZE_t sz = f_size(&fp);
		if (sz > cap)
		{
			f_close(&fp);
			return HTTPRequestEntityTooLarge;
		}
		UINT br = 0;
		FRESULT fr = f_read(&fp, pBuffer, cap, &br);
		f_close(&fp);
		if (fr != FR_OK)
			return HTTPInternalServerError;

		*ppContentType = "application/octet-stream";
		*pLength = br;
		return HTTPOK;
	}

	return HTTPNotFound;
}
