#ifndef _ENCODER_H_
#define _ENCODER_H_ 1

#include <tuple>
#include <vector>

#include "dedupdef.h"

struct DedupData;

// unsigned long long EncodeMutex(DedupData&);
// std::tuple<unsigned long long, std::vector<Globals::SmartFIFOTSV>> EncodeSmart(DedupData&);
unsigned long long EncodeSmart(DedupData&);
unsigned long long EncodeDefault(DedupData&);

#endif /* !_ENCODER_H_ */
