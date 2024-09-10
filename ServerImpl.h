// ServerImpl.h

#ifndef _ServerImpl_h
#define _ServerImpl_h

#if defined(ARDUINO) && ARDUINO >= 100
#include "arduino.h"
#else
#include "WProgram.h"
#endif

#include "VFS.h"
#include "Authentication.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <string>
#include <vector>

namespace srv {
	class Server;
}

class srv::Server
{
public:
	struct Route
	{
		using RequestHandler = void (Server::*)(AsyncWebServerRequest*);
		using UploadHandler = void (Server::*)(AsyncWebServerRequest*, const String&, size_t, uint8_t*, size_t, bool);
		using BodyHandler = void (Server::*)(AsyncWebServerRequest*, uint8_t*, size_t, size_t, size_t);

		Route(const std::string& uri, WebRequestMethodComposite method, RequestHandler requestHandler, UploadHandler uploadHandler = nullptr, BodyHandler bodyHandler = nullptr)
			: uri(uri), method(method), requestHandler(requestHandler), uploadHandler(uploadHandler), bodyHandler(bodyHandler)
		{
		}

		std::string uri;
		WebRequestMethodComposite method;
		RequestHandler requestHandler;
		UploadHandler uploadHandler;
		BodyHandler bodyHandler;
	};

	Server(vfs::Filesystem& vfs, Authentication& auth, const std::string& privateKeyPath, const std::string& publicKeyPath, uint16_t port = 80);

	// REST API
	void handleGetFile(AsyncWebServerRequest* request); // GET
	void handleUploadFile(AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final); // PUT
	void handleUploadEnd(AsyncWebServerRequest* request);
	void handleDeleteFile(AsyncWebServerRequest* request); // DELETE
	void handleCreateDirectory(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total); // POST
	void handleRenameFile(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total); // PATCH

	void handleLogin(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total); // POST
	void handleDeleteUser(AsyncWebServerRequest* request); // DELETE
	void handleGetUser(AsyncWebServerRequest* request); // GET
	void handleUpdateUser(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total); // PATCH
	void handleCreateUser(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total); // POST

	void placeholder(AsyncWebServerRequest* request);
	void handleNotFound(AsyncWebServerRequest* request);

	void setPrivateKeyFile(const std::string& path) { m_privateKeyFile = path; }
	void setPublicKeyFile(const std::string& path) { m_publicKeyFile = path; }

private:
	vfs::Filesystem& m_vfs;
	Authentication& m_auth;
	AsyncWebServer m_server;
	std::string m_privateKeyFile;
	std::string m_publicKeyFile;

	ArRequestHandlerFunction fn(void (Server::* func)(AsyncWebServerRequest* request));
	ArUploadHandlerFunction fn(void (Server::* func)(AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final));
	ArBodyHandlerFunction fn(void (Server::* func)(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total));

	std::int64_t getUserId(AsyncWebServerRequest* request);
	std::int64_t getRequestItemId(AsyncWebServerRequest* request);
	String generateErrorResponse(int code, const std::string& domain, const std::string& message);
	std::string generateJWT(Authentication::UserData& user);
	JsonDocument parseJson(uint8_t* data, size_t len);
};

#endif

