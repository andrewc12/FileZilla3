#ifndef FILEZILLA_ENGINE_HTTP_HTTPCONTROLSOCKET_HEADER
#define FILEZILLA_ENGINE_HTTP_HTTPCONTROLSOCKET_HEADER

#include "../controlsocket.h"

#include <libfilezilla/http/request.hpp>

#include <libfilezilla/file.hpp>

namespace PrivCommand {
auto const http_request = Command::private1;
auto const http_connect = Command::private2;
}

class HttpRequest : public fz::http::request
{
public:
	enum flags2 {
		flag_update_transferstatus = 0x08,
	};
};

class HttpResponse;
class HttpRequestResponseInterface
{
public:
	virtual ~HttpRequestResponseInterface() = default;

	virtual HttpRequest & request() = 0;
	virtual HttpResponse & response() = 0;
};

class HttpResponse : public fz::http::with_headers
{
public:
	// Use a writer if you need more.
	static size_t constexpr max_simple_body_size{1024 * 1024 * 16};

	unsigned int code_{};

	enum flags {
		flag_got_code = 0x01,
		flag_got_header = 0x02,
		flag_got_body = 0x04,
		flag_no_body = 0x08, // e.g. on HEAD requests, or 204/304 responses
		flag_ignore_body = 0x10 // If set, on_data_ isn't called
	};
	int flags_{};

	bool got_code() const { return flags_ & flag_got_code; }
	bool got_header() const { return flags_ & flag_got_header; }
	bool got_body() const { return (flags_ & (flag_got_body | flag_no_body | flag_ignore_body)) == flag_got_body; }
	bool no_body() const { return flags_ & flag_no_body; }

	// Called once the complete header has been received.
	// Return one of:
	//   FZ_REPLY_CONTINUE: All is well
	//   FZ_REPLY_OK: We're not interested in the request body, but continue
	//   FZ_REPLY_ERROR: Abort connection
	std::function<int(std::shared_ptr<HttpRequestResponseInterface> const&)> on_header_;

	// Writer isn't called if !success()
	std::unique_ptr<fz::writer_base> writer_;

	// Holds error body and success body if there is no writer.
	fz::buffer body_;
	size_t max_body_size_{16 * 1024 * 1024};

	bool success() const {
		return code_ >= 200 && code_ < 300;
	}

	bool code_prohobits_body() const {
		return (code_ >= 100 && code_ < 200) || code_ == 304 || code_ == 204;
	}

	virtual int reset();
};

template<typename Request, typename Response>
class ProtocolRequestResponse : public HttpRequestResponseInterface
{
public:
	virtual HttpRequest & request() override final {
		return request_;
	}

	virtual HttpResponse & response() override final {
		return response_;
	}

	Request request_;
	Response response_;
};

typedef ProtocolRequestResponse<HttpRequest, HttpResponse> HttpRequestResponse;

template<typename T> void null_deleter(T *) {}

template<typename R = HttpRequestResponseInterface, typename T>
std::shared_ptr<R> make_simple_rr(T * rr)
{
	return std::shared_ptr<R>(rr, &null_deleter<R>);
}

namespace fz {
class tls_layer;
}

class RequestThrottler final
{
public:
	void throttle(std::string const& hostname, fz::datetime const& backoff);
	fz::duration get_throttle(std::string const& hostname);

private:
	fz::mutex mtx_{false};
	std::vector<std::pair<std::string, fz::datetime>> backoff_;
};

class CHttpControlSocket : public CRealControlSocket
{
public:
	CHttpControlSocket(CFileZillaEnginePrivate & engine);
	virtual ~CHttpControlSocket();

	void FileTransfer(CHttpRequestCommand const& command);

protected:
	virtual void Connect(CServer const& server, Credentials const& credentials) override;
	virtual void FileTransfer(CFileTransferCommand const& cmd) override;

	void Request(std::shared_ptr<HttpRequestResponseInterface> const& request);
	void Request(std::deque<std::shared_ptr<HttpRequestResponseInterface>> && requests);

	template<typename T>
	void RequestMany(T && requests)
	{
		std::deque<std::shared_ptr<HttpRequestResponseInterface>> rrs;
		for (auto const& request : requests) {
			rrs.push_back(request);
		}
		Request(std::move(rrs));
	}

	// FZ_REPLY_OK: Re-using existing connection
	// FZ_REPLY_WOULDBLOCK: Cannot perform action right now (!allowDisconnect)
	// FZ_REPLY_CONTINUE: Connection operation pusehd to stack
	int InternalConnect(std::wstring const& host, unsigned short port, bool tls, bool allowDisconnect);
	virtual int Disconnect() override;

	virtual bool SetAsyncRequestReply(CAsyncRequestNotification *pNotification) override;

	std::unique_ptr<fz::tls_layer> tls_layer_;

	virtual void OnConnect() override;
	virtual void OnSocketError(int error) override;
	virtual void OnReceive() override;
	virtual int OnSend() override;

	virtual void ResetSocket() override;

	virtual void SetSocketBufferSizes() override;

	friend class CProtocolOpData<CHttpControlSocket>;
	friend class CHttpFileTransferOpData;
	friend class CHttpInternalConnectOpData;
	friend class CHttpRequestOpData;
	friend class CHttpConnectOpData;

private:
	std::wstring connected_host_;
	unsigned short connected_port_{};
	bool connected_tls_{};

	static RequestThrottler throttler_;
};

typedef CProtocolOpData<CHttpControlSocket> CHttpOpData;

std::string get_host_header(fz::uri const& uri);

#endif
