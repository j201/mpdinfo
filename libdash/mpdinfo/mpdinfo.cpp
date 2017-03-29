#include <windows.h>
#include <fstream>
#include "libdash.h"
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

using namespace dash;
using namespace dash::mpd;
using namespace dash::network;
using namespace dash::metrics;

// Finds the mime type for the given adaptation set and looks for whether the
// given string is a substring of it. Used for finding if an adaptation set
// contains video or audio.
bool isContainedInMimeType(dash::mpd::IAdaptationSet *adaptationSet, std::string value) {
	std::string topMimeType = adaptationSet->GetMimeType();
	if (!topMimeType.empty())
		if (topMimeType.find(value) != std::string::npos)
			return true;

	for (size_t i = 0; i < adaptationSet->GetRepresentation().size(); i++) {
		std::string mimeType = adaptationSet->GetRepresentation().at(i)->GetMimeType();
		if (!mimeType.empty())
			if (mimeType.find(value) != std::string::npos)
				return true;
	}

	return false;
}

// If the value exists, returns a human-readable, indented string representation
std::string showProperty(const char* name, std::string value) {
	if (value.empty()) return std::string("");
	std::ostringstream os;
	os << "  " << name << ": " << value << std::endl;
	return os.str();
}

// Convert a vector of strings into a comma-separated string
std::string showVector(const std::vector<std::string>& v) {
	std::ostringstream os;
	for (size_t i = 0; i < v.size(); i++) {
		os << v[i];
		if (i < v.size()-1) {
			os << ",";
		}
	}
	return os.str();
}

// Returns human-readable information for a representation base object
std::string representationBaseInfo(IRepresentationBase *r) {
	std::ostringstream os;
	os << "  Resolution: " << r->GetWidth() << "x" << r->GetHeight() << std::endl;
	os << showProperty("SAR", r->GetSar());
	os << showProperty("Frame Rate", r->GetFrameRate());
	os << showProperty("Audio Sample Rate", r->GetAudioSamplingRate());
	os << showProperty("MimeType", r->GetMimeType());
	os << showProperty("Codecs", showVector(r->GetCodecs()));
	os << showProperty("Profiles", showVector(r->GetProfiles()));
	os << showProperty("Segment Profiles", showVector(r->GetSegmentProfiles()));
	return os.str();
}

// Returns human-readable information for a segment object, handling different
// kinds of segments. (Inputs are skipped if null)
std::string segmentInfo(ISegmentTemplate* segmentTemplate, ISegmentBase* segmentBase, ISegmentList* segmentList) {
	std::ostringstream os;
	if (segmentTemplate) {
		os << showProperty("Segment Template", segmentTemplate->Getmedia());
	}

	if (segmentList) {
		std::vector<ISegmentURL *> segmentURLs = segmentList->GetSegmentURLs();
		for (size_t i = 0; i < segmentURLs.size(); i++) {
			if (!segmentURLs[i]->GetMediaURI().empty())
				os << "    Segment " << i << " media URI: " << segmentURLs[i]->GetMediaURI() << std::endl;
			if (!segmentURLs[i]->GetIndexURI().empty())
				os << "    Segment " << i << " index URI: " << segmentURLs[i]->GetIndexURI() << std::endl;
		}
	}

	if (segmentBase) {
		if (segmentBase->GetInitialization())
			os << showProperty("Segment Base Initialization URL", segmentBase->GetInitialization()->GetSourceURL());
		if (segmentBase->GetRepresentationIndex())
			os << showProperty("Segment Base Index URL", segmentBase->GetRepresentationIndex()->GetSourceURL());
	}
	return os.str();
}

// Finds all of the base URLs for an adaptation set. If no base URL exists,
// uses the path that the MPD was downloaded from.
std::vector<IBaseUrl*> allBaseURLs(IMPD *mpd, IPeriod *period, IAdaptationSet *adaptationSet) {
	std::vector<IBaseUrl*> baseURLs;

	if (!mpd->GetBaseUrls().empty())
		baseURLs.push_back(mpd->GetBaseUrls().at(0));
	if (!period->GetBaseURLs().empty())
		baseURLs.push_back(period->GetBaseURLs().at(0));
	if (!adaptationSet->GetBaseURLs().empty())
		baseURLs.push_back(adaptationSet->GetBaseURLs().at(0));

	// TODO: This is a bad check for absolute URLs (excludes FTP, IP addresses, possibly others)
	if (baseURLs.empty() ||
			(baseURLs.at(0)->GetUrl().substr(0,5) != "http:" && baseURLs.at(0)->GetUrl().substr(0,6) != "https:")) {
		baseURLs.insert(baseURLs.begin(), mpd->GetMPDPathBaseUrl());
	}

	return baseURLs;
}

template <typename T>
void pushIfNotNull(std::vector<T> &v, T t) {
	if (t)
		v.push_back(t);
}

// Find all segments for a representation using a base segment representation
// (i.e., not a list or a template)
std::vector<ISegment*> baseSegments(std::vector<IBaseUrl*>& baseURLs, ISegmentBase* segmentBase, IRepresentation *representation) {
	std::vector<ISegment*> segments;

	if (segmentBase->GetInitialization())
		pushIfNotNull(segments, segmentBase->GetInitialization()->ToSegment(baseURLs));

	if (segmentBase->GetRepresentationIndex())
		pushIfNotNull(segments, segmentBase->GetRepresentationIndex()->ToSegment(baseURLs));

	std::vector<IBaseUrl*> representationURLs = representation->GetBaseURLs();
	for (size_t i = 0; i < representationURLs.size(); i++)
		pushIfNotNull(segments, representationURLs[i]->ToMediaSegment(baseURLs));

	return segments;
}

// Find all segments for a representation using a segment list
std::vector<ISegment*> listSegments(std::vector<IBaseUrl*>& baseURLs, ISegmentList* segmentList) {
	std::vector<ISegment*> segments;

	if (segmentList->GetInitialization())
		pushIfNotNull(segments, segmentList->GetInitialization()->ToSegment(baseURLs));

	if (segmentList->GetBitstreamSwitching())
		pushIfNotNull(segments, segmentList->GetBitstreamSwitching()->ToSegment(baseURLs));

	std::vector<ISegmentURL*> segmentURLs = segmentList->GetSegmentURLs();
	for (size_t i = 0; i < segmentURLs.size(); i++) {
		pushIfNotNull(segments, segmentURLs[i]->ToIndexSegment(baseURLs));
		pushIfNotNull(segments, segmentURLs[i]->ToMediaSegment(baseURLs));
	}

	return segments;
}

// Find all segments for a representation using a segment template, except if
// an unspecified number of segments are to be played, in which case only the
// first one is returned
std::vector<ISegment*> templateSegments(std::vector<IBaseUrl*>& baseURLs, ISegmentTemplate* segmentTemplate, IRepresentation *representation) {
	std::vector<ISegment*> segments;

	if (segmentTemplate->GetInitialization()) {
		pushIfNotNull(segments, segmentTemplate->GetInitialization()->ToSegment(baseURLs));
	} else {
		pushIfNotNull(segments, segmentTemplate->ToInitializationSegment(baseURLs, representation->GetId(), representation->GetBandwidth()));
	}

	if (segmentTemplate->GetBitstreamSwitching()) {
		pushIfNotNull(segments, segmentTemplate->GetBitstreamSwitching()->ToSegment(baseURLs));
	} else {
		pushIfNotNull(segments, segmentTemplate->ToBitstreamSwitchingSegment(baseURLs, representation->GetId(), representation->GetBandwidth()));
	}

	if (segmentTemplate->GetSegmentTimeline()) {
		// Calculate segment start times
		std::vector<uint32_t> startTimes;

		std::vector<ITimeline*> timelines = segmentTemplate->GetSegmentTimeline()->GetTimelines();
		for (size_t i = 0; i < timelines.size(); i++) {
			for (uint32_t j = 0; j <= timelines[i]->GetRepeatCount(); j++) {
				startTimes.push_back(timelines[i]->GetStartTime() + j * timelines[i]->GetDuration());
			}
		}

		for (size_t i = 0; i < timelines.size(); i++) {
			pushIfNotNull(segments, segmentTemplate->GetIndexSegmentFromTime(baseURLs, representation->GetId(), representation->GetBandwidth(), startTimes[i]));
			pushIfNotNull(segments, segmentTemplate->GetMediaSegmentFromTime(baseURLs, representation->GetId(), representation->GetBandwidth(), startTimes[i]));
		}
	} else {
		// In some cases, nSegments could be calculated from the segment duration and overall duration,
		// but the overall duration isn't already given, in which case the video seems to continue indefinitely.
		uint32_t nSegments = 1;
		for (uint32_t i = 0; i < nSegments; i++) {
			pushIfNotNull(segments, segmentTemplate->GetIndexSegmentFromNumber(baseURLs, representation->GetId(), representation->GetBandwidth(), segmentTemplate->GetStartNumber() + i));
			pushIfNotNull(segments, segmentTemplate->GetMediaSegmentFromNumber(baseURLs, representation->GetId(), representation->GetBandwidth(), segmentTemplate->GetStartNumber() + i));
		}
	}

	return segments;
}

// Find all segments for a representation
std::vector<ISegment*> representationSegments(std::vector<IBaseUrl*>& baseURLs, IMPD *mpd, IPeriod *period, IAdaptationSet *adaptationSet, IRepresentation *representation) {
	if (representation->GetSegmentList()) {
		return listSegments(baseURLs, representation->GetSegmentList());
	} else if (representation->GetSegmentTemplate()) {
		return templateSegments(baseURLs, representation->GetSegmentTemplate(), representation);
	} else if (representation->GetSegmentBase()) {
		return baseSegments(baseURLs, representation->GetSegmentBase(), representation);
	} else if (adaptationSet->GetSegmentList()) {
		return listSegments(baseURLs, adaptationSet->GetSegmentList());
	} else if (adaptationSet->GetSegmentTemplate()) {
		return templateSegments(baseURLs, adaptationSet->GetSegmentTemplate(), representation);
	} else if (adaptationSet->GetSegmentBase()) {
		return baseSegments(baseURLs, adaptationSet->GetSegmentBase(), representation);
	} else if (period->GetSegmentList()) {
		return listSegments(baseURLs, period->GetSegmentList());
	} else if (period->GetSegmentTemplate()) {
		return templateSegments(baseURLs, period->GetSegmentTemplate(), representation);
	} else if (period->GetSegmentBase()) {
		return baseSegments(baseURLs, period->GetSegmentBase(), representation);
	} else if (!representation->GetBaseURLs().empty()) {
		std::vector<ISegment*> segments;
		std::vector<IBaseUrl*> representationURLs = representation->GetBaseURLs();
		for (size_t i = 0; i < representationURLs.size(); i++) {
			segments.push_back(representationURLs[i]->ToMediaSegment(baseURLs));
		}
		return segments;
	}
	return std::vector<ISegment*>();
}

// Print out human-readable information about a representation and its segments
void dumpRepresentationInfo(IRepresentation *r) {
	std::cout << "Representation " << r->GetId() << std::endl;
	std::cout << "  Bandwidth: " << r->GetBandwidth() << "bps" << std::endl;
	std::cout << representationBaseInfo(r);
	const std::vector<IBaseUrl *> baseURLs = r->GetBaseURLs();
	if (!baseURLs.empty()) {
		std::cout << "  Base URLs:" << std::endl;
		for (size_t i = 0; i < baseURLs.size(); i++) {
			std::cout << "    " << baseURLs[i]->GetUrl() << std::endl;
		}
	}

	std::cout << segmentInfo(r->GetSegmentTemplate(), r->GetSegmentBase(), r->GetSegmentList());
}

// A tracker class for use with a libav IDownloadableChunk.
// Used to track segment downloading
class DownloadTracker : public IDownloadObserver {
private:
	uint64_t startTime;

public:
	DownloadState state = NOT_STARTED;
	uint64_t dlTime; // in ms
	uint64_t bytesDownloaded;

	void OnDownloadRateChanged  (uint64_t bytesDownloaded) {
		this->bytesDownloaded = bytesDownloaded;
	}

	void OnDownloadStateChanged (DownloadState state) {
		this->state = state;
		switch(state) {
			case IN_PROGRESS:
				this->startTime = GetTickCount64();
				break;
			case COMPLETED:
			case ABORTED:
				this->dlTime = GetTickCount64() - this->startTime;
				break;
			default:
				break;
		}
	}
};

// Writes out the data for the given segment to a temp file and return its path.
// The segment must have been already downloaded.
std::string writeToTempFile(ISegment* s, size_t size) {
	FILE* f;
	if (tmpfile_s(&f)) {
		std::cerr << "Error creating temp file" << std::endl;
		return "";
	}

	char tempPath[MAX_PATH];
	char fileName[MAX_PATH];
	GetTempPathA(MAX_PATH, tempPath);
	GetTempFileNameA(tempPath, "", 0, fileName);

	int bytesLeft = size;
	int bytesRead = 0;
	const int bufferSize = 32768;
	uint8_t* buffer = new uint8_t[bufferSize];
	std::ofstream fs;
	fs.open(fileName, std::ios::binary | std::ios::out);
	do {
		bytesRead = s->Read(buffer, min(bufferSize, bytesLeft));
		fs.write((char*)buffer, bytesRead);
	} while (bytesLeft > 0 && bytesRead > 0);
	fs.close();

	return std::string(fileName);
}

// Decodes the header of the given file and prints out the information on it
// given by libav
void decoderInfo(std::string fileName) {
	std::cout << std::endl << "Decoding video" << std::endl;

	AVFormatContext* ctx = avformat_alloc_context();

	if (avformat_open_input(&ctx, fileName.c_str(), NULL, NULL) < 0) {
		std::cout << "Error opening video" << std::endl;
		return;
	}
	std::cout << "Opened video" << std::endl;
	avformat_find_stream_info(ctx, NULL);
	av_dump_format(ctx, 0, fileName.c_str(), 0);

    avformat_close_input(&ctx);
}

// Downloads a segment and prints out download metrics and decoding results
// Prints out downloaded file location and doesn't delete it if preserve is true
void downloadSegment(ISegment* s, bool preserve) {
	DownloadTracker downloadTracker;
	s->AttachDownloadObserver(&downloadTracker);
	s->StartDownload();
	while (downloadTracker.state != COMPLETED && downloadTracker.state != ABORTED)
		Sleep(100);

	if (downloadTracker.state == ABORTED) {
		std::cout << "Download aborted" << std::endl;
	} else {
		std::cout << std::endl;
		std::cout << "Download time: " << downloadTracker.dlTime << "ms" << std::endl;
		std::cout << "Download size: " << downloadTracker.bytesDownloaded << "B" << std::endl;
		std::cout << "Download rate: " << (float)downloadTracker.bytesDownloaded * 8 / downloadTracker.dlTime << "kbps" << std::endl;
		std::vector<IHTTPTransaction*> transactions = s->GetHTTPTransactionList();
		for (size_t i = 0; i < transactions.size(); i++) {
			std::cout << "Download succeeeded from " << transactions[i]->OriginalUrl() << std::endl;
			std::cout << "Response code: " << transactions[i]->ResponseCode() << std::endl;
			std::cout << "HTTP Header: " << transactions[i]->HTTPHeader() << std::endl;
		}

		std::string fileName = writeToTempFile(s, downloadTracker.bytesDownloaded);
		if (preserve)
			std::cout << "Wrote downloaded video to file " << fileName << std::endl;
		if (!fileName.empty())
			decoderInfo(fileName);
		if (!preserve) {
			if (remove(fileName.c_str()) != 0)
				std::cerr << "Failed to delete temp file";
		}
	}
}

int main(int argc, char *argv[]) {
	if (argc < 2) {
		std::cerr << "Usage: mpdinfo.exe <MPD URL> [-h] [--r-index N] [--r-id ID] [--preserve]" << std::endl;
		return 1;
	}
	char* URL = NULL;
	int rIndex = -1;
	char* rID = NULL;
	bool preserve = false;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			std::cerr << "Usage: mpdinfo.exe <MPD URL> [-h] [--r-index N] [--r-id ID]" << std::endl;
			std::cerr << "--r-index: Selects a representation to download by index" << std::endl;
			std::cerr << "--r-id: Selects a representation to download by ID" << std::endl;
			std::cerr << "--preserve: Preserves downloaded video files and prints out their locations" << std::endl;
			return 0;
		} else if (strcmp(argv[i], "--r-index") == 0) {
			rIndex = atoi(argv[i+1]);
			if (rID != NULL) {
				std::cerr << "Only one of --r-index and --r-id can be specified" << std::endl;
				return 1;
			}
			i++;
		} else if (strcmp(argv[i], "--r-id") == 0) {
			rID = argv[i+1];
			if (rIndex != -1) {
				std::cerr << "Only one of --r-index and --r-id can be specified" << std::endl;
				return 1;
			}
			i++;
		} else if (strcmp(argv[i], "--preserve") == 0) {
			preserve = true;
		} else {
			if (URL != NULL) {
				std::cerr << "Multiple URLs specified or an invalid argument passed" << std::endl;
				std::cerr << "Run 'mpdinfo -h' for usage" << std::endl;
				return 1;
			}
			URL = argv[i];
		}
	}
	// Default to the first representation
	if (rIndex == -1 && rID == NULL) {
		rIndex = 0;
	}
	if (URL == NULL) {
		std::cerr << "Error: no URL specified" << std::endl;
		return 1;
	}

	av_register_all();
	avformat_network_init();

	IDASHManager* dashManager = CreateDashManager();
	std::cout << URL << std::endl;
	IMPD* mpd = dashManager->Open(argv[1]);

	if (mpd == NULL) {
		std::cerr << "Failed to read MPD" << std::endl;
		return 1;
	}

	const std::vector<IBaseUrl *> baseURLs = mpd->GetBaseUrls();
	if (!baseURLs.empty()) {
		std::cout << "MPD Base URLs:" << std::endl;
		for (size_t i = 0; i < baseURLs.size(); i++) {
			std::cout << "  " << baseURLs[i]->GetUrl() << std::endl;
		}
	}

	// Iterate through the MPD periods, then adaptation sets, then
	// representations, then segments, showing information about each
	for (size_t i = 0; i < mpd->GetPeriods().size(); i++) {
		IPeriod *period = mpd->GetPeriods().at(i);
		std::cout << "Period " << i << std::endl;

		const std::vector<IBaseUrl *> baseURLs = period->GetBaseURLs();
		if (!baseURLs.empty()) {
			std::cout << "Base URLs:" << std::endl;
			for (size_t i = 0; i < baseURLs.size(); i++) {
				std::cout << "  " << baseURLs[i]->GetUrl() << std::endl;
			}
		}

		std::cout << segmentInfo(period->GetSegmentTemplate(), period->GetSegmentBase(), period->GetSegmentList());

		std::vector<IAdaptationSet *> adaptationSets = period->GetAdaptationSets();

		for (size_t i = 0; i < adaptationSets.size(); i++) {
			IAdaptationSet* adaptationSet = adaptationSets.at(i);
			if (isContainedInMimeType(adaptationSet, "video")) {
				std::cout << "Adaptation set " << i << std::endl;
				const std::vector<IBaseUrl *> baseURLs = adaptationSet->GetBaseURLs();
				if (!baseURLs.empty()) {
					std::cout << "  Base URLs:" << std::endl;
					for (size_t i = 0; i < baseURLs.size(); i++) {
						std::cout << "    " << baseURLs[i]->GetUrl() << std::endl;
					}
				}

				std::string fullBaseURL = "";
				{
					std::vector<IBaseUrl*> baseURLs = allBaseURLs(mpd, period, adaptationSet);
					for (size_t i = 0; i < baseURLs.size(); i++) {
						std::string baseURL = baseURLs.at(i)->GetUrl();
						if (fullBaseURL == "") {
							fullBaseURL = baseURL;
						} else if (baseURL != "") {
							if (baseURL.at(0) == '/' && fullBaseURL.at(fullBaseURL.size() - 1) == '/')
								fullBaseURL += baseURL.substr(1, -1);
							else if (baseURL.at(0) != '/' && fullBaseURL.at(fullBaseURL.size() - 1) != '/')
								fullBaseURL += "/" + baseURL;
						}
					}
				}
				std::cout << showProperty("Full base URL", fullBaseURL);

				std::cout << segmentInfo(adaptationSet->GetSegmentTemplate(), adaptationSet->GetSegmentBase(), adaptationSet->GetSegmentList());

				std::vector<IRepresentation *> representations = adaptationSet->GetRepresentation();

				for (size_t i = 0; i < representations.size(); i++) {
					IRepresentation* representation = representations.at(i);
					dumpRepresentationInfo(representation);
					const std::vector<ISubRepresentation *> subRepresentations = representation->GetSubRepresentations();
					for (size_t i = 0; i < subRepresentations.size(); i++) {
						std::cout << "SubRepresentation " << i << std::endl;
						std::cout << representationBaseInfo(subRepresentations.at(i));
					}
				}

				// Download the segments for a representation if required
				IRepresentation* representation = NULL;

				for (size_t i = 0; i < representations.size(); i++) {
					if (i == rIndex || representations[i]->GetId().compare(rID) == 0) {
						representation = representations[i];
						break;
					}
				}

				if (representation != NULL) {
					std::vector<IBaseUrl*> baseURLs = allBaseURLs(mpd, period, adaptationSet);
					std::vector<ISegment*> segments = representationSegments(baseURLs, mpd, period, adaptationSet, representation);
					// Just download everything in series for now
					for (size_t i = 0; i < segments.size(); i++) {
						downloadSegment(segments[i], preserve);
						delete segments[i];
					}
				}
			}
		}
	}

	return 0;
}
