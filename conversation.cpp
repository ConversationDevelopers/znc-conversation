/*
Copyright (c) 2015, Tobias Pollmann.
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

3. Neither the name of the copyright holders nor the names of its contributors
may be used to endorse or promote products derived from this software without
specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


#include <znc/IRCNetwork.h>
#include <znc/Chan.h>
#include <znc/User.h>
#include <znc/Modules.h>

#define REQUIRESSL 1

using std::set;
using std::map;
using std::vector;

class CDevice {
public:

	CDevice(const CString& sToken, CModule& Parent)
			: m_Parent(Parent), m_sToken(sToken) {
				m_uPort = 0;
	}

	virtual ~CDevice() {}

	CString GetToken() const {
		return m_sToken;
	}

	CString GetConnection() const {
		return m_sConnection;
	}

	void SetConnection(const CString& s) {
		m_sConnection = s;
	}

	CString GetHost() const {
		return m_sHost;
	}

	void SetHost(const CString& s) {
		m_sHost = s;
	}

	unsigned short GetPort() const {
		return m_uPort;
	}

	void SetPort(unsigned short u) {
		m_uPort = u;
	}

	bool Remove() {
		if (!m_Parent.DelNV("device::" + GetToken())) {
			return false;
		}
		DEBUG("Device " + GetToken() + " removed");

		return true;
	}

	CString Serialize() const {
		CString sRet(m_sToken.FirstLine() + "\n"
			+ m_sHost.FirstLine() + ":" + CString(m_uPort) + "\n"
			+ m_sConnection.FirstLine());

		return sRet;
	}

	bool Save() {
		CString sStr(Serialize());

		if (!m_Parent.SetNV("device::" + GetToken(), sStr)) {
			DEBUG("Error while saving device");
			return false;
		}

		DEBUG("Device " + GetToken() + " saved");
		return true;
	}

	CString Escape(const CString& sStr) const {
		CString sRet(sStr);

		sRet.Replace("\\", "\\\\");
		sRet.Replace("\r", "\\r");
		sRet.Replace("\n", "\\n");
		sRet.Replace("\t", "\\t");
		sRet.Replace("\a", "\\a");
		sRet.Replace("\b", "\\b");
		sRet.Replace("\e", "\\e");
		sRet.Replace("\f", "\\f");
		sRet.Replace("\"", "\\\"");

		set<char> ssBadChars;

		for (CString::iterator it = sRet.begin(); it != sRet.end(); it++) {
			if (!isprint(*it)) {
				ssBadChars.insert(*it);
			}
		}

		for (set<char>::iterator b = ssBadChars.begin(); b != ssBadChars.end(); b++) {
			sRet.Replace(CString(*b), ToHex(*b));
		}

		return sRet;
	}

	CString ToHex(const char c) const {
		return "\\u00" + CString(c).Escape_n(CString::EURL).TrimPrefix_n("%");
	}

	bool Parse(const CString& sStr) {
		VCString vsLines;
		sStr.Split("\n", vsLines);

		m_sToken = vsLines[0];
		m_sHost = vsLines[1].Token(0, false, ":");
		m_uPort = vsLines[1].Token(1, false, ":").ToUInt();
		m_sConnection = vsLines[2];

		return true;
	}

void Push(const CString& sNick, const CString& sMessage, const CString& sChannel = "", const CString& sConnection = "") {

	if (m_sToken.empty()) {
		return;
	}

	if (!m_uPort || m_sHost.empty()) {
		DEBUG("---- Push() undefined host or port!");
	}

	CString sPayload;

	sPayload = "{";

	sPayload += "\"devicetoken\":\"" + Escape(m_sToken) + "\"";

	if (!sMessage.empty()) {
		sPayload += ",\"message\":\"" + Escape(sMessage) + "\"";
	}
	// action
	if (!sNick.empty()) {
		sPayload += ",\"sender\":\"" + Escape(sNick) + "\"";
	}

	if (!sChannel.empty()) {
		sPayload += ",\"channel\":\"" + Escape(sChannel) + "\"";
	}

	if (!m_sConnection.empty()) {
		sPayload += ",\"connection\":\"" + Escape(m_sConnection) + "\"";
	}

	sPayload += "}";

	DEBUG("Send push notification to " << m_sHost << ":" << m_uPort << " with payload");
	DEBUG(sPayload);

	CSocket *pSock = new CSocket(&m_Parent);
	pSock->Connect(m_sHost, m_uPort, true);
	pSock->Write(sPayload);
	pSock->Close(Csock::CLT_AFTERWRITE);
	m_Parent.AddSocket(pSock);

}

private:
	set<CClient*> 	m_spClients;
	CModule&      	m_Parent;
	CString					m_sToken;
	CString       	m_sConnection;
	CString       	m_sHost;
	unsigned short 	m_uPort;
};

class CConversationMod : public CModule {
public:
	MODCONSTRUCTOR(CConversationMod) {
		AddHelpCommand();
		AddCommand("test", static_cast<CModCommand::ModCmdFunc>(&CConversationMod::HandleTestCommand),
			"", "Send notifications to registered devices");
		AddCommand("list", static_cast<CModCommand::ModCmdFunc>(&CConversationMod::HandleListCommand),
			"", "List all registered devices");
		AddCommand("remove", static_cast<CModCommand::ModCmdFunc>(&CConversationMod::HandleRemoveCommand),
				"<device-token>", "Removes a registered devices");

			LoadRegistry();

			for (MCString::iterator it = BeginNV(); it != EndNV(); it++) {
				CString sKey(it->first);

				if (sKey.TrimPrefix("device::")) {
					CDevice* pDevice = new CDevice(sKey, *this);

					if (!pDevice->Parse(it->second)) {
						DEBUG("  --- Error while parsing device [" + sKey + "]");
						delete pDevice;
						continue;
					}

					m_mspDevices[pDevice->GetToken()] = pDevice;
				} else {
					DEBUG("   --- Unknown registry entry: [" << it->first << "]");
				}
			}
	}

	virtual ~CConversationMod() {
		for (map<CString, CDevice*>::iterator it = m_mspDevices.begin(); it != m_mspDevices.end(); it++) {
			it->second->Save();
			delete it->second;
		}
	}

	CDevice* FindDevice(const CString& sToken) {
		map<CString, CDevice*>::iterator it = m_mspDevices.find(sToken);

		if (it != m_mspDevices.end()) {
			return it->second;
		}

		return NULL;
	}

	void RemoveDevice(const CString& sToken) {
		map<CString, CDevice*> devices;

		for (map<CString, CDevice*>::iterator it = m_mspDevices.begin(); it != m_mspDevices.end(); it++) {
			CDevice *pDevice = it->second;
			if (pDevice->GetToken() != sToken) {
				devices[pDevice->GetToken()] = it->second;
			} else {
				pDevice->Remove();
			}
		}
		m_mspDevices = devices;

	}

	virtual bool OnLoad(const CString& sArgs, CString& sMessage) {
		return true;
	}

	virtual EModRet OnUserRaw(CString& sLine) {
		return HandleUserRaw(m_pClient, sLine);
	}

	virtual EModRet OnUnknownUserRaw(CClient* pClient, CString& sLine) {
		return HandleUserRaw(pClient, sLine);
	}

	virtual EModRet HandleUserRaw(CClient* pClient, CString& sLine) {

		if (sLine.TrimPrefix("CONVERSATION ")) {
			if (sLine.TrimPrefix("add-device ")) {
				CString sToken(sLine.Token(0));
				CDevice* pDevice = FindDevice(sToken);

				if (!pDevice) {
					pDevice = new CDevice(sToken, *this);
					pDevice->SetHost(sLine.Token(1));
					pDevice->SetPort(sLine.Token(2).ToUInt());
					pDevice->SetConnection(sLine.Token(3, true).TrimPrefix_n(":"));
					m_mspDevices[pDevice->GetToken()] = pDevice;
					if (!pDevice->Save()) {
						PutModule("Unable to save device " + pDevice->GetToken());
					} else {
						PutModule("Added new device " + pDevice->GetToken());
					}
				}

			} else if (sLine.TrimPrefix("remove-device ")) {
				CDevice* pDevice = FindDevice(sLine);

				if (pDevice) {
					RemoveDevice(pDevice->GetToken());
					PutModule("Removed device " + pDevice->GetToken());
				}
			}

			return HALT;
		}

		return CONTINUE;
	}

	bool Test(const CString& sKeyWord, const CString& sString) {
		return (!sKeyWord.empty() && (
			sString.Equals(sKeyWord + " ", false, sKeyWord.length() +1)
			|| sString.Right(sKeyWord.length() +1).Equals(" " + sKeyWord)
			|| sString.AsLower().WildCmp("* " + sKeyWord.AsLower() + " *")
			|| (sKeyWord.find_first_of("*?") != CString::npos && sString.AsLower().WildCmp(sKeyWord.AsLower()))
		));
	}

	void ParseMessage(CNick& Nick, CString& sMessage, CChan *pChannel = NULL) {
		if (m_pNetwork->IsUserOnline() == false) {
#if defined VERSION_MAJOR && defined VERSION_MINOR && VERSION_MAJOR >= 1 && VERSION_MINOR >= 2
			CString sCleanMessage = sMessage.StripControls();
#else
			CString &sCleanMessage = sMessage;
#endif

			for (map<CString, CDevice*>::iterator it = m_mspDevices.begin(); it != m_mspDevices.end(); it++) {
				CDevice *device = it->second;

				if (pChannel != NULL) {
					// Test our current irc nick
					const CString& sMyNick(GetNetwork()->GetIRCNick().GetNick());
					bool matches = Test(sMyNick, sMessage) || Test(sMyNick + "?*", sMessage);

					if (!matches) {
						return;
					}

				}

				device->Push(Nick.GetNick(), sCleanMessage, pChannel->GetName(), device->GetConnection());
			}
		}
	}

	virtual EModRet OnChanMsg(CNick& Nick, CChan& Channel, CString& sMessage) {
		ParseMessage(Nick, sMessage, &Channel);
		return CONTINUE;
	}

	virtual EModRet OnPrivMsg(CNick& Nick, CString& sMessage) {
		ParseMessage(Nick, sMessage, NULL);
		return CONTINUE;
	}

	void HandleTestCommand(const CString& sLine) {
		if (m_pNetwork) {
			unsigned int count = 0;

			for (map<CString, CDevice*>::iterator it = m_mspDevices.begin(); it != m_mspDevices.end(); it++) {
				CDevice *device = it->second;

					++count;
					device->Push("conversation", "Test notification");
			}

			PutModule("Notification sent to " + CString(count) + " devices.");
		} else {
			PutModule("You need to connect with a network.");
		}
	}

	void HandleListCommand(const CString &sLine) {

		CTable Table;

		Table.AddColumn("Device");
		Table.AddColumn("Connection");

		for (map<CString, CDevice*>::iterator it = m_mspDevices.begin(); it != m_mspDevices.end(); it++) {

			CDevice *device = it->second;

			Table.AddRow();
			Table.SetCell("Device", device->GetToken());
			Table.SetCell("Connection", device->GetConnection());

		}

		if (PutModule(Table) == 0) {
			PutModule("There are no devices registered.");
		}

	}

	void HandleRemoveCommand(const CString &sLine) {
		CString sToken = sLine.Token(1);
		RemoveDevice(sToken);
		PutModule("Done");
	}

private:
	map<CString, CDevice*>	m_mspDevices;
};

template<> void TModInfo<CConversationMod>(CModInfo& Info) {
				Info.AddType(CModInfo::UserModule);
				Info.SetWikiPage("conversation");
}

NETWORKMODULEDEFS(CConversationMod, "Send highlights and personal messages to Conversation for iOS")
