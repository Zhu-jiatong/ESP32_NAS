// 
// 
// 

#include "ServerImpl.h"
#include "SQLiteError.h"
#include "sstream"
#include "JWT.h"
#include <FileError.h>
#include "ServerError.h"

srv::Server::Server(vfs::Filesystem& vfs, Authentication& auth, const std::string& privateKeyPath, const std::string& publicKeyPath, std::uint16_t port)
	:m_vfs(vfs), m_auth(auth), m_server(port), m_privateKeyFile(privateKeyPath), m_publicKeyFile(publicKeyPath)
{
	m_server
		.serveStatic("/", m_vfs.getDiskMap().getDiskByID(0).getFS(), "/webpage/")
		.setDefaultFile("index.html");

	//m_server.on("/api/login", HTTP_POST, fn(&Server::placeholder), nullptr, ErrorWrapper(fn(&Server::handleLogin)));
	//m_server.on("/api/users", HTTP_POST, fn(&Server::placeholder), nullptr, ErrorWrapper(fn(&Server::handleCreateUser)));
	//m_server.on("^\\/api\\/users\\/(\\d+)$", HTTP_GET, ErrorWrapper(fn(&Server::handleGetUser)));
	//m_server.on("^\\/api\\/users\\/(\\d+)$", HTTP_DELETE, ErrorWrapper(fn(&Server::handleDeleteUser)));
	//m_server.on("^\\/api\\/users\\/(\\d+)$", HTTP_PATCH, fn(&Server::placeholder), nullptr, ErrorWrapper(fn(&Server::handleUpdateUser)));

	//m_server.on("^\\/api\\/files\\/(\\d+)$", HTTP_GET, ErrorWrapper(fn(&Server::handleGetFile)));
	//m_server.on("^\\/api\\/files\\/(\\d+)$", HTTP_DELETE, ErrorWrapper(fn(&Server::handleDeleteFile)));
	//m_server.on("^\\/api\\/files\\/(\\d+)$" /*parent directory*/, HTTP_PUT, fn(&Server::handleUploadEnd), ErrorWrapper(fn(&Server::handleUploadFile)));
	//m_server.on("^\\/api\\/files\\/(\\d+)$", HTTP_POST, fn(&Server::placeholder), nullptr, ErrorWrapper(fn(&Server::handleCreateDirectory)));
	//m_server.on("^\\/api\\/files\\/(\\d+)$", HTTP_PATCH, fn(&Server::placeholder), nullptr, ErrorWrapper(fn(&Server::handleRenameFile)));

	std::vector<Route> routes = {
		{"/api/login", HTTP_POST, &Server::placeholder, nullptr, &Server::handleLogin},
		{"/api/users", HTTP_POST, &Server::placeholder, nullptr, &Server::handleCreateUser},
		{"^\\/api\\/users\\/(\\d+)$", HTTP_GET, &Server::handleGetUser},
		{"^\\/api\\/users\\/(\\d+)$", HTTP_DELETE, &Server::handleDeleteUser},
		{"^\\/api\\/users\\/(\\d+)$", HTTP_PATCH, &Server::placeholder, nullptr, &Server::handleUpdateUser},

		{"^\\/api\\/files\\/(\\d+)$", HTTP_GET, &Server::handleGetFile},
		{"^\\/api\\/files\\/(\\d+)$", HTTP_DELETE, &Server::handleDeleteFile},
		{"^\\/api\\/files\\/(\\d+)$", HTTP_PUT, &Server::handleUploadEnd, &Server::handleUploadFile},
		{"^\\/api\\/files\\/(\\d+)$", HTTP_POST, &Server::placeholder, nullptr, &Server::handleCreateDirectory},
		{"^\\/api\\/files\\/(\\d+)$", HTTP_PATCH, &Server::placeholder, nullptr, &Server::handleRenameFile}
	};

	for (const Route& route : routes)
		m_server.on(
			route.uri.c_str(),
			route.method,
			ErrorWrapper(fn(route.requestHandler)),
			route.uploadHandler ? static_cast<ArUploadHandlerFunction>(ErrorWrapper(fn(route.uploadHandler))) : nullptr,
			route.bodyHandler ? static_cast<ArBodyHandlerFunction>(ErrorWrapper(fn(route.bodyHandler))) : nullptr
		);

	m_server.onNotFound(fn(&Server::handleNotFound));

	m_server.begin();
}

void srv::Server::handleGetFile(AsyncWebServerRequest* request)
{
	std::int64_t id = getRequestItemId(request);

	if (m_vfs.isDirectory(id))
	{
		std::vector<vfs::Filesystem::FileMetadata> entries = m_vfs.listDirectory(id);
		JsonDocument doc;
		JsonArray data = doc["data"].to<JsonArray>();
		for (const auto& entry : entries)
		{
			JsonObject obj = data.add<JsonObject>();
			obj["id"] = entry.fileID;
			obj["name"] = entry.name;
			obj["isDirectory"] = entry.isDirectory;
			obj["ownerID"] = entry.ownerID;
			if (entry.isDirectory)
				continue;
			obj["size"] = entry.size;
			obj["lastModified"] = entry.lastModified;
		}
		doc.shrinkToFit();
		String response;
		serializeJson(doc, response);
		return request->send(200, "application/json", response);
	}

	FS& fs = m_vfs.getDisk(id).getFS();
	std::string path = m_vfs.getInternalPath(id);
	request->send(fs, path.c_str(), "application/octet-stream"); // TODO: Determine MIME type
}

void srv::Server::handleUploadFile(AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final)
{
	if (!index)
	{
		if (!request->hasParam("size"))
			throw std::invalid_argument("Missing size parameter");

		const String& sizeStr = request->getParam("size")->value();
		std::uint64_t size;
		std::istringstream(sizeStr.c_str()) >> size;

		std::int64_t parentID = getRequestItemId(request);

		std::int64_t userID = getUserId(request);

		request->_tempFile = m_vfs.openFile(parentID, filename.c_str(), size, userID);
	}

	if (len)
		request->_tempFile.write(data, len);

	if (final)
		request->_tempObject = request->beginResponse(200);
}

void srv::Server::handleUploadEnd(AsyncWebServerRequest* request)
{
	if (!request->_tempObject)
		return;

	AsyncWebServerResponse* response = reinterpret_cast<AsyncWebServerResponse*>(request->_tempObject);
	request->send(response);
	request->_tempObject = nullptr;
}

void srv::Server::handleDeleteFile(AsyncWebServerRequest* request)
{
	std::int64_t id = getRequestItemId(request);

	std::int64_t userID = getUserId(request);
	m_vfs.removeFileEntry(id, userID);
	return request->send(200);
}

void srv::Server::handleCreateDirectory(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total)
{
	JsonDocument doc = parseJson(data, len);
	if (!doc.containsKey("name"))
		throw std::invalid_argument("Missing name field in request body");

	std::string name = doc["name"];

	std::int64_t parentID = getRequestItemId(request);

	std::int64_t userID = getUserId(request);
	m_vfs.createNewDirectoryEntry(parentID, name, userID);

	request->_tempObject = request->beginResponse(200);
}

void srv::Server::handleRenameFile(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total)
{
	JsonDocument doc = parseJson(data, len);
	if (!doc.containsKey("newName"))
		throw std::invalid_argument("Missing newName field in request body");

	std::string newName = doc["newName"];

	std::int64_t id = getRequestItemId(request);

	std::int64_t userID = getUserId(request);
	m_vfs.renameFileEntry(id, newName, userID);

	request->_tempObject = request->beginResponse(200);
}

void srv::Server::handleLogin(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total)
{
	JsonDocument doc = parseJson(data, len);
	if (!doc.containsKey("username"))
		throw std::invalid_argument("Missing username field in request body");
	if (!doc.containsKey("password"))
		throw std::invalid_argument("Missing password field in request body");

	std::string username = doc["username"];
	std::string password = doc["password"];

	std::optional<Authentication::UserData> userData = m_auth.authenticate(username, password);
	if (!userData)
		throw std::invalid_argument("Invalid username or password"); // TODO: new error type for this

	const std::string JWTContent = generateJWT(*userData);

	request->_tempObject = request->beginResponse(200, "application/json", JWTContent.c_str());
}

void srv::Server::handleDeleteUser(AsyncWebServerRequest* request)
{
	std::int64_t id = getRequestItemId(request);
	std::int64_t requestUserID = getUserId(request);
	if (requestUserID != id)
		throw std::invalid_argument("Cannot delete another user");

	m_auth.deleteUser(id);
	request->send(200);
}

void srv::Server::handleGetUser(AsyncWebServerRequest* request)
{
	std::int64_t id = getRequestItemId(request);
	std::int64_t requestUserID = getUserId(request);
	if (requestUserID != id)
		throw std::invalid_argument("Cannot get another user");

	std::optional<Authentication::UserData> userData = m_auth.getUserData(id);
	if (!userData)
		throw std::invalid_argument("User not found");

	Authentication::UserData& data = *userData;

	JsonDocument doc;
	JsonObject user = doc["data"].to<JsonObject>();
	user["id"] = data.id;
	user["username"] = data.username;
	doc.shrinkToFit();
	String response;
	serializeJson(doc, response);
	request->send(200, "application/json", response);
}

void srv::Server::handleUpdateUser(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total)
{
	std::int64_t id = getRequestItemId(request);
	std::int64_t requestUserID = getUserId(request);
	if (requestUserID != id)
		throw std::invalid_argument("Cannot update another user");

	JsonDocument doc = parseJson(data, len);
	if (doc.containsKey("username"))
	{
		std::string username = doc["username"];
		m_auth.updateUserUsername(id, username);
	}

	if (doc.containsKey("password"))
	{
		std::string password = doc["password"];
		m_auth.updateUserPassword(id, password);
	}

	request->_tempObject = request->beginResponse(200);
}

void srv::Server::handleCreateUser(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total)
{
	JsonDocument doc = parseJson(data, len);
	if (!doc.containsKey("username"))
		throw std::invalid_argument("Missing username field in request body");
	if (!doc.containsKey("password"))
		throw std::invalid_argument("Missing password field in request body");

	std::string username = doc["username"];
	std::string password = doc["password"];

	m_auth.createUser(username, password);
	request->_tempObject = request->beginResponse(200);
}

void srv::Server::placeholder(AsyncWebServerRequest* request)
{
	if (!request->_tempObject)
		throw std::invalid_argument("Missing reqeust body");

	AsyncWebServerResponse* response = reinterpret_cast<AsyncWebServerResponse*>(request->_tempObject);
	request->send(response);
	request->_tempObject = nullptr;
}

void srv::Server::handleNotFound(AsyncWebServerRequest* request)
{
	JsonDocument doc;
	JsonObject error = doc["error"].to<JsonObject>();
	error["code"] = 404;
	error["domain"] = "Server";
	error["message"] = "Resource Not Found";
	error["location"] = request->url();
	error["locationType"] = "Request URI";

	String response;
	serializeJson(doc, response);
	request->send(404, "application/json", response);
}

ArRequestHandlerFunction srv::Server::fn(void(Server::* func)(AsyncWebServerRequest* request))
{
	return std::bind(func, this, std::placeholders::_1);
}

ArUploadHandlerFunction srv::Server::fn(void(Server::* func)(AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final))
{
	return std::bind(func, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6);
	//return [this, func](AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final)
	//	{
	//		(this->*func)(request, filename, index, data, len, final);
	//	};
}

ArBodyHandlerFunction srv::Server::fn(void(Server::* func)(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total))
{
	return std::bind(func, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5);
	//return [this, func](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total)
	//	{
	//		(this->*func)(request, data, len, index, total);
	//	};
}

std::int64_t srv::Server::getUserId(AsyncWebServerRequest* request)
{
	if (!request->hasHeader("Authorization"))
		throw std::invalid_argument("Missing Authorization header");

	const String& header = request->header("Authorization");
	const String token = header.substring(strlen("Bearer "));
	const JWT::JWTContent jwtContent = JWT::parse(token.c_str());

	const std::string jwtBody = token.substring(0, token.lastIndexOf('.')).c_str();
	const bool isValid = JWT::verifyWithKeyFile({ jwtBody.begin(), jwtBody.end() }, jwtContent.signature, m_publicKeyFile);
	if (!isValid)
		throw std::invalid_argument("Invalid JWT signature");

	JsonDocument doc;
	const DeserializationError error = deserializeJson(doc, jwtContent.payload);
	if (error)
		throw std::invalid_argument(error.c_str());
	if (!doc.containsKey("id"))
		throw std::invalid_argument("Missing sub field in JWT payload");

	return doc["id"];
}

std::int64_t srv::Server::getRequestItemId(AsyncWebServerRequest* request)
{
	const String& idStr = request->pathArg(0);
	std::int64_t id;
	std::istringstream(idStr.c_str()) >> id;
	return id;
}

std::string srv::Server::generateJWT(Authentication::UserData& user)
{
	std::string header;
	std::string payload;

	{
		JsonDocument JWTHeader;
		JWTHeader["alg"] = "RS256";
		JWTHeader["typ"] = "JWT";
		JWTHeader.shrinkToFit();
		serializeJson(JWTHeader, header);
	}

	{
		JsonDocument JWTPayload;
		JWTPayload["id"] = user.id;
		JWTPayload["username"] = user.username;
		JWTPayload.shrinkToFit();
		serializeJson(JWTPayload, payload);
	}

	const std::string base64Header = JWT::base64URLEncode({ header.begin(), header.end() });
	const std::string base64Payload = JWT::base64URLEncode({ payload.begin(), payload.end() });
	const std::string JWTBody = base64Header + '.' + base64Payload;

	const JWT::ByteData signature = JWT::signWithKeyFile({ JWTBody.begin(), JWTBody.end() }, m_privateKeyFile);
	const std::string base64Signature = JWT::base64URLEncode(signature);

	return JWTBody + '.' + base64Signature;
}

JsonDocument srv::Server::parseJson(uint8_t* data, size_t len)
{
	std::string body(reinterpret_cast<char*>(data), len);
	JsonDocument doc;
	DeserializationError error = deserializeJson(doc, body);
	if (error)
		throw error; // TODO: implement wrapper class inheriting from std::exception
	return doc;
}
