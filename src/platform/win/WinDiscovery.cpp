#include "WinPlatform.h"
#include "WinText.h"
#include <windns.h>
#include <ws2tcpip.h>

// Finding MicroKeys on other machines over Bonjour (DNS-SD on multicast
// DNS), with the service API Windows 10 and later have built in.  Each copy
// that takes messages advertises "<name>._microkeys._tcp.local".

#define SERVICE_TYPE	L"_microkeys._tcp.local"

class Advertised {
	// The registration in effect, kept until Windows says it's undone
public:
	DNS_SERVICE_REGISTER_REQUEST Request = { 0 };
	PDNS_SERVICE_INSTANCE Instance = NULL;
	wstring Name;
	int Port = 0;
};

static mutex _adLock;
static Advertised* _advertised = nullptr;

static VOID WINAPI Registered(DWORD status, PVOID context, PDNS_SERVICE_INSTANCE instance) {
	// Windows hands back its own copy of the instance
	if (instance != NULL) {
		DnsServiceFreeInstance(instance);
	}
}

static VOID WINAPI Unregistered(DWORD status, PVOID context, PDNS_SERVICE_INSTANCE instance) {
	Advertised* ad = (Advertised*)context;
	if (instance != NULL) {
		DnsServiceFreeInstance(instance);
	}
	if (ad->Instance != NULL) {
		DnsServiceFreeInstance(ad->Instance);
	}
	delete ad;
}

static string HostName() {
	wchar_t name[256];
	DWORD size = 256;
	if (!GetComputerNameExW(ComputerNameDnsHostname, name, &size)) {
		return "localhost";
	}
	return Utf16ToUtf8(name, size);
}

bool WinPlatform::Advertise(const string& name, int port, string& error) {
	lock_guard<mutex> hold(_adLock);
	wstring wide = Utf8ToUtf16(name);
	if (_advertised != nullptr) {
		if (_advertised->Name == wide && _advertised->Port == port) {
			return true;
		}
		// Unregistered() frees it once Windows is done with it
		Advertised* old = _advertised;
		_advertised = nullptr;
		old->Request.pRegisterCompletionCallback = Unregistered;
		if (DnsServiceDeRegister(&old->Request, NULL) != DNS_REQUEST_PENDING) {
			if (old->Instance != NULL) {
				DnsServiceFreeInstance(old->Instance);
			}
			delete old;
		}
	}
	if (name.empty()) {
		return true;
	}

	auto ad = new Advertised();
	ad->Name = wide;
	ad->Port = port;
	wstring service = wide + L"." SERVICE_TYPE;
	wstring host = Utf8ToUtf16(HostName()) + L".local";
	ad->Instance = DnsServiceConstructInstance(service.c_str(), host.c_str(), NULL, NULL, (WORD)port, 0, 0, 0, NULL, NULL);
	if (ad->Instance == NULL) {
		delete ad;
		error = "unable to describe the service";
		return false;
	}
	ad->Request.Version = DNS_QUERY_REQUEST_VERSION1;
	ad->Request.pServiceInstance = ad->Instance;
	ad->Request.pRegisterCompletionCallback = Registered;
	ad->Request.pQueryContext = ad;
	DWORD status = DnsServiceRegister(&ad->Request, NULL);
	if (status != DNS_REQUEST_PENDING) {
		DnsServiceFreeInstance(ad->Instance);
		delete ad;
		error = LastErrorText(status);
		return false;
	}
	_advertised = ad;
	return true;
}

class Lookup {
	// What a resolve or a browse found, shared with its callback, which may
	// run after the caller has stopped waiting
public:
	mutex Lock;
	condition_variable Done;
	bool Finished = false;
	string Host;
	int Port = 0;
	vector<string> Names;
};

static VOID WINAPI Resolved(DWORD status, PVOID context, PDNS_SERVICE_INSTANCE instance) {
	auto ref = (shared_ptr<Lookup>*)context;
	shared_ptr<Lookup> lookup = *ref;
	delete ref;
	{
		lock_guard<mutex> hold(lookup->Lock);
		if (status == ERROR_SUCCESS && instance != NULL) {
			char text[INET6_ADDRSTRLEN] = { 0 };
			if (instance->ip4Address != NULL) {
				InetNtopA(AF_INET, instance->ip4Address, text, sizeof(text));
			}
			else if (instance->ip6Address != NULL) {
				InetNtopA(AF_INET6, instance->ip6Address, text, sizeof(text));
			}
			lookup->Host = text[0] != 0 ? string(text) : instance->pszHostName != NULL ? Utf16ToUtf8(instance->pszHostName) : "";
			lookup->Port = instance->wPort;
		}
		lookup->Finished = true;
	}
	lookup->Done.notify_all();
	if (instance != NULL) {
		DnsServiceFreeInstance(instance);
	}
}

bool WinPlatform::Resolve(const string& name, int timeoutMs, string& host, int& port) {
	auto lookup = make_shared<Lookup>();
	wstring query = Utf8ToUtf16(name) + L"." SERVICE_TYPE;
	DNS_SERVICE_RESOLVE_REQUEST request = { 0 };
	request.Version = DNS_QUERY_REQUEST_VERSION1;
	request.QueryName = (PWSTR)query.c_str();
	request.pResolveCompletionCallback = Resolved;
	request.pQueryContext = new shared_ptr<Lookup>(lookup);
	DNS_SERVICE_CANCEL cancel = { 0 };
	if (DnsServiceResolve(&request, &cancel) != DNS_REQUEST_PENDING) {
		delete (shared_ptr<Lookup>*)request.pQueryContext;
		return false;
	}
	unique_lock<mutex> hold(lookup->Lock);
	if (!lookup->Done.wait_for(hold, chrono::milliseconds(timeoutMs), [&] { return lookup->Finished; })) {
		hold.unlock();
		DnsServiceResolveCancel(&cancel);
		return false;
	}
	if (lookup->Host.empty() || lookup->Port == 0) {
		return false;
	}
	host = lookup->Host;
	port = lookup->Port;
	return true;
}

static VOID WINAPI Browsed(DWORD status, PVOID context, PDNS_RECORD records) {
	// Each answer names one instance, "<name>._microkeys._tcp.local".  The
	// callback keeps coming until the browse is cancelled.
	auto ref = (shared_ptr<Lookup>*)context;
	shared_ptr<Lookup> lookup = *ref;
	if (status != ERROR_SUCCESS) {
		delete ref;
	}
	lock_guard<mutex> hold(lookup->Lock);
	for (PDNS_RECORD rec = records; rec != NULL; rec = rec->pNext) {
		if (rec->wType != DNS_TYPE_PTR || rec->Data.PTR.pNameHost == NULL) {
			continue;
		}
		wstring full = rec->Data.PTR.pNameHost;
		size_t end = full.find(L"._microkeys.");
		if (end != wstring::npos && end > 0) {
			string name = Utf16ToUtf8(full.substr(0, end));
			if (find(lookup->Names.begin(), lookup->Names.end(), name) == lookup->Names.end()) {
				lookup->Names.push_back(name);
			}
		}
	}
	if (records != NULL) {
		DnsRecordListFree(records, DnsFreeRecordList);
	}
}

vector<string> WinPlatform::Browse(int timeoutMs) {
	auto lookup = make_shared<Lookup>();
	DNS_SERVICE_BROWSE_REQUEST request = { 0 };
	request.Version = DNS_QUERY_REQUEST_VERSION1;
	request.QueryName = SERVICE_TYPE;
	request.pBrowseCallback = Browsed;
	request.pQueryContext = new shared_ptr<Lookup>(lookup);
	DNS_SERVICE_CANCEL cancel = { 0 };
	if (DnsServiceBrowse(&request, &cancel) != DNS_REQUEST_PENDING) {
		delete (shared_ptr<Lookup>*)request.pQueryContext;
		return vector<string>();
	}
	Sleep(timeoutMs > 0 ? timeoutMs : 0);
	DnsServiceBrowseCancel(&cancel);
	lock_guard<mutex> hold(lookup->Lock);
	return lookup->Names;
}
