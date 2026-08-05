#include "CefViewSchemeHandler.h"

CefViewSchemeHandler::CefViewSchemeHandler(CefRefPtr<CefBrowser> browser,
                                           CefRefPtr<CefFrame> frame,
                                           CefViewBrowserClientDelegateInterface::RefPtr delegate)
  : browser_(browser)
  , frame_(frame)
  , handler_delegate_(delegate)
  , offset_(0)
{
}

CefViewSchemeHandler::~CefViewSchemeHandler() {}

bool
CefViewSchemeHandler::Open(CefRefPtr<CefRequest> request, bool& handle_request, CefRefPtr<CefCallback> callback)
{
  // DCHECK(!CefCurrentlyOn(TID_UI) && !CefCurrentlyOn(TID_IO));

  handle_request = true;

  auto browserDelegate = handler_delegate_.lock();
  if (browserDelegate) {
    CefString cefStrUrl = request->GetURL();
    browserDelegate->processUrlRequest(browser_, frame_, cefStrUrl.ToString());
  }

  data_ = "ok";
  mime_type_ = "text/html";

  return true;
}

void
CefViewSchemeHandler::GetResponseHeaders(CefRefPtr<CefResponse> response,
                                         int64_t& response_length,
                                         CefString& redirectUrl)
{
  CEF_REQUIRE_IO_THREAD();

  DCHECK(!data_.empty());
  response->SetMimeType(mime_type_);
  response->SetStatus(200);
  // Set the resulting response length
  response_length = data_.length();
}

bool
CefViewSchemeHandler::Skip(int64_t bytes_to_skip, int64_t& bytes_skipped, CefRefPtr<CefResourceSkipCallback> callback)
{
  // CEF_REQUIRE_IO_THREAD();

  bytes_skipped = 0;

  const int64_t data_len = static_cast<int64_t>(data_.length());
  const int64_t current_offset = offset_;
  const int64_t available = data_len - current_offset;

  if (available <= 0) {
    // Already at (or past) the end of the data: nothing left to skip.
    offset_ = static_cast<int>(data_len);
  } else if (bytes_to_skip < available) {
    // Skip only part of the remaining data.
    offset_ += static_cast<int>(bytes_to_skip);
    bytes_skipped = bytes_to_skip;
  } else {
    // Skip everything remaining up to the end of the data.
    bytes_skipped = available;
    offset_ = static_cast<int>(data_len);
  }

  return true;
}

bool
CefViewSchemeHandler::Read(void* data_out,
                           int bytes_to_read,
                           int& bytes_read,
                           CefRefPtr<CefResourceReadCallback> callback)
{
  // DCHECK(!CefCurrentlyOn(TID_UI) && !CefCurrentlyOn(TID_IO));

  bytes_read = 0;
  const int64_t data_len = static_cast<int64_t>(data_.length());
  const int64_t current_offset = offset_;
  if (current_offset < data_len) {
    // Copy the next block of data into the buffer. transfer_size is bounded by
    // bytes_to_read (an int), so it is safe to fall back to int here.
    const int64_t available = data_len - current_offset;
    const int64_t transfer_size = std::min<int64_t>(bytes_to_read, available);
    memcpy(data_out, data_.c_str() + current_offset, static_cast<size_t>(transfer_size));
    offset_ = static_cast<int>(current_offset + transfer_size);
    bytes_read = static_cast<int>(transfer_size);
  }

  return bytes_read > 0;
}

void
CefViewSchemeHandler::Cancel()
{
  // CEF_REQUIRE_IO_THREAD();
}
