#include "libdash.h"

using namespace dash;
using namespace dash::mpd;

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


std::vector<dash::mpd::IBaseUrl*> allBaseURLs(IMPD *mpd, IPeriod *period, IAdaptationSet *adaptationSet) {
	std::vector<dash::mpd::IBaseUrl*> baseURLs;

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
					dumpRepresentationInfo(representations.at(i));
					const std::vector<ISubRepresentation *> subRepresentations = representations.at(i)->GetSubRepresentations();
					for (size_t i = 0; i < subRepresentations.size(); i++) {
						std::cout << "SubRepresentation " << i << std::endl;
						std::cout << representationBaseInfo(subRepresentations.at(i));
					}
				}
			}
		}
	}

	return 0;
}
