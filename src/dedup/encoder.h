#ifndef _ENCODER_H_
#define _ENCODER_H_ 1

struct DedupData;

unsigned long long EncodeMutex(DedupData&);
unsigned long long EncodeSmart(DedupData&);
unsigned long long EncodeDefault(DedupData&);

#endif /* !_ENCODER_H_ */
