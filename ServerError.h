#pragma once

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "SQLiteError.h"
#include "VFSError.h"
#include "FileError.h"
#include "DiskError.h"
#include <functional>

template<typename T>
class ErrorWrapper
{
public:
	ErrorWrapper(T requestHandler) : m_requestHandler(requestHandler) {}

	template<typename... Args>
	void operator()(AsyncWebServerRequest* request, Args&&... args)
	{
		try
		{
			m_requestHandler(request, std::forward<Args>(args)...);
		}
		catch (const SQLite::SQLiteError& e)
		{
			JsonDocument doc;
			JsonObject error = doc["error"].to<JsonObject>();
			error["code"] = 500;
			error["domain"] = "SQLite";
			error["message"] = e.what();
			handleError(request, std::forward<Args>(args)..., 500, doc);
		}
		catch (vfs::DiskError& e)
		{
			JsonDocument doc;
			JsonObject error = doc["error"].to<JsonObject>();
			error["code"] = 500;
			error["domain"] = "Disk";
			error["message"] = e.what();
			if (e.hasDisk())
			{
				error["location"] = e.getDisk().getMountpoint();
				error["locationType"] = "Disk Mount Point";
			}
			handleError(request, std::forward<Args>(args)..., 500, doc);
		}
		catch (const vfs::FileError& e)
		{
			JsonDocument doc;
			JsonObject error = doc["error"].to<JsonObject>();
			error["code"] = 500;
			error["domain"] = "File";
			error["message"] = e.what();
			if (e.hasFile())
			{
				error["location"] = e.getFile().fileID;
				error["locationType"] = "File ID";
			}
			handleError(request, std::forward<Args>(args)..., 500, doc);
		}
		catch (const vfs::VFSError& e)
		{
			JsonDocument doc;
			JsonObject error = doc["error"].to<JsonObject>();
			error["code"] = 500;
			error["domain"] = "VFS";
			error["message"] = e.what();
			handleError(request, std::forward<Args>(args)..., 500, doc);
		}
		catch (const DeserializationError& e)
		{
			JsonDocument doc;
			JsonObject error = doc["error"].to<JsonObject>();
			error["code"] = 400;
			error["domain"] = "JSON";
			error["message"] = e.c_str();
			handleError(request, std::forward<Args>(args)..., 400, doc);
		}
		catch (const std::exception& e)
		{
			JsonDocument doc;
			JsonObject error = doc["error"].to<JsonObject>();
			error["code"] = 500;
			error["domain"] = "Unknown";
			error["message"] = e.what();
			handleError(request, std::forward<Args>(args)..., 500, doc);
		}
		catch (...)
		{
			JsonDocument doc;
			JsonObject error = doc["error"].to<JsonObject>();
			error["code"] = 500;
			error["domain"] = "Unknown";
			error["message"] = "Unknown error";
			handleError(request, std::forward<Args>(args)..., 500, doc);
		}
	}

private:
	T m_requestHandler;

	template<typename... Args>
	void handleError(AsyncWebServerRequest* request, Args&&..., int code, JsonDocument& doc)
	{
		doc.shrinkToFit();
		String response;
		serializeJson(doc, response);
		request->send(code, "application/json", response);
	}

	template<typename... Args>
	void handleError(AsyncWebServerRequest* request, uint8_t*, size_t, size_t, size_t, Args&&..., int code, JsonDocument& doc)
	{
		doc.shrinkToFit();
		String response;
		serializeJson(doc, response);
		request->_tempObject = request->beginResponse(code, "application/json", response);
	}

	template<typename... Args>
	void handleError(AsyncWebServerRequest* request, const String&, size_t, uint8_t*, size_t, bool, Args&&..., int code, JsonDocument& doc)
	{
		doc.shrinkToFit();
		String response;
		serializeJson(doc, response);
		request->send(code, "application/json", response);
	}
};