#include <windows.h>
#include "libdash.h"

using namespace dash;
using namespace dash::mpd;
using namespace dash::network;
using namespace dash::metrics;

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

std::string showProperty(const char* name, std::string value) {
	if (value.empty()) return std::string("");
	std::ostringstream os;
	os << "  " << name << ": " << value << std::endl;
	return os.str();
}

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
		uint32_t nSegments = 1; // TODO: calculate from overall time and segment duration
		for (uint32_t i = 0; i < nSegments; i++) {
			pushIfNotNull(segments, segmentTemplate->GetIndexSegmentFromNumber(baseURLs, representation->GetId(), representation->GetBandwidth(), segmentTemplate->GetStartNumber() + i));
			pushIfNotNull(segments, segmentTemplate->GetMediaSegmentFromNumber(baseURLs, representation->GetId(), representation->GetBandwidth(), segmentTemplate->GetStartNumber() + i));
		}
	}

	return segments;
}


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

void dumpTransactionInfo(IHTTPTransaction *t) {
}

class DownloadTracker : public IDownloadObserver {
private:
	uint64_t startTime;

public:
	DownloadState state = NOT_STARTED;
	uint64_t dlTime; // in ms

	void OnDownloadRateChanged  (uint64_t bytesDownloaded) {}
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

void downloadSegment(ISegment* s) {
	DownloadTracker downloadTracker;
	s->AttachDownloadObserver(&downloadTracker);
	s->StartDownload();
	while (downloadTracker.state != COMPLETED && downloadTracker.state != ABORTED)
		Sleep(100);

	std::cout << std::endl;
	std::cout << "Download time: " << downloadTracker.dlTime << "ms" << std::endl;
	std::vector<IHTTPTransaction*> transactions = s->GetHTTPTransactionList();
	for (size_t i = 0; i < transactions.size(); i++) {
		std::cout << "Download ended from " << transactions[i]->OriginalUrl() << std::endl;
		std::cout << "Response code: " << transactions[i]->ResponseCode() << std::endl;
		std::cout << "HTTP Header: " << transactions[i]->HTTPHeader() << std::endl;
	}
}

int main(int argc, char *argv[]) {
	if (argc != 2) {
		std::cerr << "Usage: mpdinfo.exe <MPD URL>" << std::endl;
		return 1;
	}

	IDASHManager* dashManager = CreateDashManager();
	std::cout << argv[1] << std::endl;
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

				if (!representations.empty()) {
					// For now, only try to download the first representation
					IRepresentation* representation = representations[0];

					std::vector<IBaseUrl*> baseURLs = allBaseURLs(mpd, period, adaptationSet);
					std::vector<ISegment*> segments = representationSegments(baseURLs, mpd, period, adaptationSet, representation);
					// Just download everything in series for now
					for (size_t i = 0; i < segments.size(); i++)
						downloadSegment(segments[i]);
				}
			}
		}
	}

	return 0;
}
