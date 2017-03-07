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
					IRepresentation *representation = representations.at(i);

					std::cout << representation->GetId() << " "
						<< representation->GetBandwidth() << " bps "
						<< representation->GetWidth() << "x" << representation->GetHeight()
						<< std::endl;
				}
			}
		}
	}

	return 0;
}
