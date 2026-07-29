#ifndef __INC_SAM_BASE_H
#define __INC_SAM_BASE_H

#include "model.h"


class SamBase : public Model {
public:
    virtual cv::Mat generate_mask_from_image_embedding(ImageEmbedding &image_embedding, const Prompt &prompt) = 0;

    GenerateResponse generate(GenerateRequest &request) override;
};
#endif //__INC_SAM_BASE_H