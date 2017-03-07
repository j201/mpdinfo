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

std::string showVector(const std::vector<std::string>& v) {
	std::ostringstream os;
	for (int i = 0; i < v.size(); i++) {
		os << v[i];
		if (i < v.size()-1) {
			os << ",";
		}
	}
	return os.str();
}

std::string dumpRepresentationBaseInfo(IRepresentationBase *r) {
	std::ostringstream os;
	os << "  Resolution: " << r->GetWidth() << "x" << r->GetHeight() << std::endl;
	os << "  SAR: " << r->GetSar() << std::endl;
	os << "  Frame Rate: " << r->GetFrameRate() << std::endl;
	os << "  Audio Sample Rate: " << r->GetAudioSamplingRate() << std::endl;
	os << "  MimeType: " << r->GetMimeType() << std::endl;
	os << "  Codecs: " << showVector(r->GetCodecs()) << std::endl;
	os << "  Profiles: " << showVector(r->GetProfiles()) << std::endl;
	os << "  Segment Profiles: " << showVector(r->GetSegmentProfiles()) << std::endl;
	return os.str();
}

void dumpRepresentationInfo(IRepresentation *r) {
	std::cout << "Representation " << r->GetId() << std::endl;
	std::cout << "  Bandwidth: " << r->GetBandwidth() << "bps" << std::endl;
	std::cout << dumpRepresentationBaseInfo(r);
	const std::vector<IBaseUrl *> baseURLs = r->GetBaseURLs();
	std::cout << "  Base URLs:" << std::endl;
	for (int i = 0; i < baseURLs.size(); i++) {
		std::cout << "    " + baseURLs[i]->GetUrl();
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

	if (mpd->GetPeriods().size() > 0) {
		IPeriod *period = mpd->GetPeriods().at(0);

		std::vector<IAdaptationSet *> adaptationSets = period->GetAdaptationSets();

		for (size_t i = 0; i < adaptationSets.size(); i++) {
			if (isContainedInMimeType(adaptationSets.at(i), "video")) {
				std::cout << "Adaptation set " << i << std::endl;
				std::vector<IRepresentation *> representations = adaptationSets.at(i)->GetRepresentation();

				for (size_t i = 0; i < representations.size(); i++) {
					dumpRepresentationInfo(representations.at(i));
				}
			}
		}
	}

	return 0;
}
