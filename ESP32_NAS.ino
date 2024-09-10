/*
 Name:		ESP32_NAS.ino
 Created:	8/27/2024 5:57:06 PM
 Author:	zhuji
*/

#include "ServerImpl.h"
#include <WiFi.h>
#include <SD.h>
#include <ESPmDNS.h>
#include <memory>

const std::string privateKeyPath = "/sd/jwt_key.pem";
const std::string publicKeyPath = "/sd/jwt_key.pub.pem";

std::unique_ptr<srv::Server> server;
std::unique_ptr<vfs::Filesystem> filesystem;
std::unique_ptr<Authentication> auth;
std::unique_ptr<SQLite::DbConnection> db;

// the setup function runs once when you press reset or power the board
void setup()
{
	Serial.begin(115200);

	try
	{
		if (!SD.begin(SS, SPI, 80000000))
			throw std::runtime_error("SD card initialization failed");

		if (SD.exists("/nas.db"))
			SD.remove("/nas.db");

		if (!WiFi.softAP("ESP32_NAS"))
			throw std::runtime_error("WiFi initialization failed");

		if (!MDNS.begin("nas"))
			throw std::runtime_error("mDNS initialization failed");

		db = std::make_unique<SQLite::DbConnection>("/sd/nas.db");
		initialiseDatabase(*db);

		filesystem = std::make_unique<vfs::Filesystem>(*db);
		db->beforeRowUpdate<vfs::Filesystem>(vfs::Filesystem::preupdateCallback, *filesystem);
		vfs::Disk disk(SD, "/sd", std::bind(&SDFS::totalBytes, &SD), std::bind(&SDFS::usedBytes, &SD));
		filesystem->getDiskMap().mountDisk(0, disk);

		auth = std::make_unique<Authentication>(*db, std::bind(&vfs::Filesystem::createRootDirectoryEntry, filesystem.get(), std::placeholders::_1));

		server = std::make_unique<srv::Server>(*filesystem, *auth, privateKeyPath, publicKeyPath);

		if (!MDNS.addService("http", "tcp", 80))
			throw std::runtime_error("Failed to add mDNS service");

		Serial.println("DOne");
	}
	catch (const std::exception& e)
	{
		Serial.println(e.what());
	}
}

// the loop function runs over and over again until power down or reset
void loop()
{

}

void initialiseDatabase(SQLite::DbConnection& db)
{
	db.prepare(
		"PRAGMA foreign_keys = ON"
	).evaluate();

	db.prepare(
		"CREATE TABLE IF NOT EXISTS Disks ("
		"ID INTEGER PRIMARY KEY,"
		"Mountpoint TEXT NOT NULL UNIQUE"
		")"
	).evaluate();

	db.prepare(
		"CREATE TABLE IF NOT EXISTS Users ("
		"ID INTEGER PRIMARY KEY AUTOINCREMENT,"
		"Username TEXT NOT NULL UNIQUE,"
		"PasswordHash BLOB NOT NULL"
		")"
	).evaluate();

	db.prepare(
		"CREATE TABLE IF NOT EXISTS FileEntries ("
		"ID INTEGER PRIMARY KEY AUTOINCREMENT,"
		"OwnerID INTEGER NOT NULL,"
		"DiskID INTEGER,"
		"ParentID INTEGER,"
		"Name TEXT,"

		"UNIQUE (ParentID, Name),"
		"FOREIGN KEY (OwnerID) REFERENCES Users(ID) ON DELETE CASCADE,"
		"FOREIGN KEY (DiskID) REFERENCES Disks(ID),"
		"FOREIGN KEY (ParentID) REFERENCES FileEntries(ID) ON DELETE CASCADE"
		")"
	).evaluate();

	db.prepare(
		"CREATE TABLE IF NOT EXISTS UserFilePermissions ("
		"UserID INTEGER NOT NULL,"
		"FileID INTEGER NOT NULL,"
		"Permission TEXT NOT NULL,"

		"PRIMARY KEY (UserID, FileID, Permission),"
		"FOREIGN KEY (UserID) REFERENCES Users(ID) ON DELETE CASCADE,"
		"FOREIGN KEY (FileID) REFERENCES FileEntries(ID) ON DELETE CASCADE"
		")"
	).evaluate();
}
