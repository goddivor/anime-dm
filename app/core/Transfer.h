#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/Download.h"

class Http;

// The video a source resolved, and the headers its host demands.
struct TransferSource {
    std::string url;
    std::map<std::string, std::string> headers;
};

// What the caller uses to steer a transfer and what the transfer reports.
struct TransferControl {
    std::atomic<bool>& stop;
    std::atomic<bool>& discard;
    DownloadError error = DownloadError::None;
    std::string detail;
};

// Called a few times a second while the parts come in.
using TransferProgress =
    std::function<void(uint64_t done, uint64_t total, double fraction, double speed)>;

// One way of fetching a video into parts, then assembling them.
class Transfer {
public:
    virtual ~Transfer() = default;

    // Learns what the host offers and reloads a saved position, if any.
    // False when this video cannot be fetched this way; the caller tries the
    // next one.
    virtual bool Prepare() = 0;

    // Fetches the parts. False when stopped or failed; the control says which.
    virtual bool Run(int connections, const TransferProgress& progress) = 0;

    // Joins the parts into the final file.
    virtual bool Assemble(const std::wstring& outPath) = 0;

    // The extension the assembled file should carry, with its dot.
    virtual std::wstring Extension() const = 0;

    // The size in bytes when known ahead, zero otherwise.
    virtual uint64_t Total() const = 0;
};

// Builds the transfer that fits a video: a playlist is fetched segment by
// segment, anything else by byte ranges. Nothing when the host refuses it.
std::unique_ptr<Transfer> OpenTransfer(Http& http, const TransferSource& source,
                                       const std::wstring& partsDir, TransferControl& control);
