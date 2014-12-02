/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "GrDrawState.h"

#include "GrBlend.h"
#include "GrOptDrawState.h"
#include "GrPaint.h"
#include "GrProcOptInfo.h"

//////////////////////////////////////////////////////////////////////////////s

bool GrDrawState::isEqual(const GrDrawState& that) const {
    bool usingVertexColors = this->hasColorVertexAttribute();
    if (!usingVertexColors && this->fColor != that.fColor) {
        return false;
    }

    if (this->getRenderTarget() != that.getRenderTarget() ||
        this->fColorStages.count() != that.fColorStages.count() ||
        this->fCoverageStages.count() != that.fCoverageStages.count() ||
        !this->fViewMatrix.cheapEqualTo(that.fViewMatrix) ||
        this->fSrcBlend != that.fSrcBlend ||
        this->fDstBlend != that.fDstBlend ||
        this->fBlendConstant != that.fBlendConstant ||
        this->fFlagBits != that.fFlagBits ||
        this->fVACount != that.fVACount ||
        this->fVAStride != that.fVAStride ||
        memcmp(this->fVAPtr, that.fVAPtr, this->fVACount * sizeof(GrVertexAttrib)) ||
        this->fStencilSettings != that.fStencilSettings ||
        this->fDrawFace != that.fDrawFace) {
        return false;
    }

    bool usingVertexCoverage = this->hasCoverageVertexAttribute();
    if (!usingVertexCoverage && this->fCoverage != that.fCoverage) {
        return false;
    }

    bool explicitLocalCoords = this->hasLocalCoordAttribute();
    if (this->hasGeometryProcessor()) {
        if (!that.hasGeometryProcessor()) {
            return false;
        } else if (!this->getGeometryProcessor()->isEqual(*that.getGeometryProcessor())) {
            return false;
        }
    } else if (that.hasGeometryProcessor()) {
        return false;
    }

    for (int i = 0; i < this->numColorStages(); i++) {
        if (!GrFragmentStage::AreCompatible(this->getColorStage(i), that.getColorStage(i),
                                             explicitLocalCoords)) {
            return false;
        }
    }
    for (int i = 0; i < this->numCoverageStages(); i++) {
        if (!GrFragmentStage::AreCompatible(this->getCoverageStage(i), that.getCoverageStage(i),
                                             explicitLocalCoords)) {
            return false;
        }
    }

    SkASSERT(0 == memcmp(this->fFixedFunctionVertexAttribIndices,
                         that.fFixedFunctionVertexAttribIndices,
                         sizeof(this->fFixedFunctionVertexAttribIndices)));

    return true;
}

GrDrawState::CombinedState GrDrawState::CombineIfPossible(
    const GrDrawState& a, const GrDrawState& b, const GrDrawTargetCaps& caps) {

    if (!a.isEqual(b)) {
        return kIncompatible_CombinedState;
    }

    // If the general draw states are equal (from check above) we know hasColorVertexAttribute()
    // is equivalent for both a and b
    if (a.hasColorVertexAttribute()) {
        // If one is opaque and the other is not then the combined state is not opaque. Moreover,
        // if the opaqueness affects the ability to get color/coverage blending correct then we
        // don't combine the draw states.
        bool aIsOpaque = (kVertexColorsAreOpaque_Hint & a.fHints);
        bool bIsOpaque = (kVertexColorsAreOpaque_Hint & b.fHints);
        if (aIsOpaque != bIsOpaque) {
            const GrDrawState* opaque;
            const GrDrawState* nonOpaque;
            if (aIsOpaque) {
                opaque = &a;
                nonOpaque = &b;
            } else {
                opaque = &b;
                nonOpaque = &a;
            }
            if (!opaque->hasSolidCoverage() && opaque->couldApplyCoverage(caps)) {
                SkASSERT(!nonOpaque->hasSolidCoverage());
                if (!nonOpaque->couldApplyCoverage(caps)) {
                    return kIncompatible_CombinedState;
                }
            }
            return aIsOpaque ? kB_CombinedState : kA_CombinedState;
        }
    }
    return kAOrB_CombinedState;
}

//////////////////////////////////////////////////////////////////////////////s

GrDrawState::GrDrawState(const GrDrawState& state, const SkMatrix& preConcatMatrix) {
    SkDEBUGCODE(fBlockEffectRemovalCnt = 0;)
    *this = state;
    if (!preConcatMatrix.isIdentity()) {
        for (int i = 0; i < this->numColorStages(); ++i) {
            fColorStages[i].localCoordChange(preConcatMatrix);
        }
        for (int i = 0; i < this->numCoverageStages(); ++i) {
            fCoverageStages[i].localCoordChange(preConcatMatrix);
        }
    }
}

GrDrawState& GrDrawState::operator=(const GrDrawState& that) {
    SkASSERT(0 == fBlockEffectRemovalCnt || 0 == this->numTotalStages());
    SkASSERT(!that.fRenderTarget.ownsPendingIO());
    SkASSERT(!this->fRenderTarget.ownsPendingIO());
    this->setRenderTarget(that.getRenderTarget());
    fColor = that.fColor;
    fViewMatrix = that.fViewMatrix;
    fSrcBlend = that.fSrcBlend;
    fDstBlend = that.fDstBlend;
    fBlendConstant = that.fBlendConstant;
    fFlagBits = that.fFlagBits;
    fVACount = that.fVACount;
    fVAPtr = that.fVAPtr;
    fVAStride = that.fVAStride;
    fStencilSettings = that.fStencilSettings;
    fCoverage = that.fCoverage;
    fDrawFace = that.fDrawFace;
    if (that.hasGeometryProcessor()) {
        fGeometryProcessor.initAndRef(that.fGeometryProcessor);
    } else {
        fGeometryProcessor.reset(NULL);
    }
    fColorStages = that.fColorStages;
    fCoverageStages = that.fCoverageStages;

    fHints = that.fHints;

    fColorProcInfoValid = that.fColorProcInfoValid;
    fCoverageProcInfoValid = that.fCoverageProcInfoValid;
    if (fColorProcInfoValid) {
        fColorProcInfo = that.fColorProcInfo;
    }
    if (fCoverageProcInfoValid) {
        fCoverageProcInfo = that.fCoverageProcInfo;
    }

    memcpy(fFixedFunctionVertexAttribIndices,
            that.fFixedFunctionVertexAttribIndices,
            sizeof(fFixedFunctionVertexAttribIndices));
    return *this;
}

void GrDrawState::onReset(const SkMatrix* initialViewMatrix) {
    SkASSERT(0 == fBlockEffectRemovalCnt || 0 == this->numTotalStages());
    SkASSERT(!fRenderTarget.ownsPendingIO());

    fGeometryProcessor.reset(NULL);
    fColorStages.reset();
    fCoverageStages.reset();

    fRenderTarget.reset();

    this->setDefaultVertexAttribs();

    fColor = 0xffffffff;
    if (NULL == initialViewMatrix) {
        fViewMatrix.reset();
    } else {
        fViewMatrix = *initialViewMatrix;
    }
    fSrcBlend = kOne_GrBlendCoeff;
    fDstBlend = kZero_GrBlendCoeff;
    fBlendConstant = 0x0;
    fFlagBits = 0x0;
    fStencilSettings.setDisabled();
    fCoverage = 0xff;
    fDrawFace = kBoth_DrawFace;

    fHints = 0;

    fColorProcInfoValid = false;
    fCoverageProcInfoValid = false;
}

bool GrDrawState::setIdentityViewMatrix()  {
    if (this->numFragmentStages()) {
        SkMatrix invVM;
        if (!fViewMatrix.invert(&invVM)) {
            // sad trombone sound
            return false;
        }
        for (int s = 0; s < this->numColorStages(); ++s) {
            fColorStages[s].localCoordChange(invVM);
        }
        for (int s = 0; s < this->numCoverageStages(); ++s) {
            fCoverageStages[s].localCoordChange(invVM);
        }
    }
    fViewMatrix.reset();
    return true;
}

void GrDrawState::setFromPaint(const GrPaint& paint, const SkMatrix& vm, GrRenderTarget* rt) {
    SkASSERT(0 == fBlockEffectRemovalCnt || 0 == this->numTotalStages());

    fGeometryProcessor.reset(NULL);
    fColorStages.reset();
    fCoverageStages.reset();

    for (int i = 0; i < paint.numColorStages(); ++i) {
        fColorStages.push_back(paint.getColorStage(i));
    }

    for (int i = 0; i < paint.numCoverageStages(); ++i) {
        fCoverageStages.push_back(paint.getCoverageStage(i));
    }

    this->setRenderTarget(rt);

    fViewMatrix = vm;

    // These have no equivalent in GrPaint, set them to defaults
    fBlendConstant = 0x0;
    fDrawFace = kBoth_DrawFace;
    fStencilSettings.setDisabled();
    this->resetStateFlags();
    fHints = 0;

    // Enable the clip bit
    this->enableState(GrDrawState::kClip_StateBit);

    this->setColor(paint.getColor());
    this->setState(GrDrawState::kDither_StateBit, paint.isDither());
    this->setState(GrDrawState::kHWAntialias_StateBit, paint.isAntiAlias());

    this->setBlendFunc(paint.getSrcBlendCoeff(), paint.getDstBlendCoeff());
    this->setCoverage(0xFF);
    fColorProcInfoValid = false;
    fCoverageProcInfoValid = false;
}

////////////////////////////////////////////////////////////////////////////////

bool GrDrawState::validateVertexAttribs() const {
    // check consistency of effects and attributes
    GrSLType slTypes[kMaxVertexAttribCnt];
    for (int i = 0; i < kMaxVertexAttribCnt; ++i) {
        slTypes[i] = static_cast<GrSLType>(-1);
    }

    if (this->hasGeometryProcessor()) {
        const GrGeometryProcessor* gp = this->getGeometryProcessor();
        // make sure that any attribute indices have the correct binding type, that the attrib
        // type and effect's shader lang type are compatible, and that attributes shared by
        // multiple effects use the same shader lang type.
        const GrGeometryProcessor::VertexAttribArray& s = gp->getVertexAttribs();

        int effectIndex = 0;
        for (int index = 0; index < fVACount; index++) {
            if (kGeometryProcessor_GrVertexAttribBinding != fVAPtr[index].fBinding) {
                // we only care about effect bindings
                continue;
            }
            SkASSERT(effectIndex < s.count());
            GrSLType effectSLType = s[effectIndex].getType();
            GrVertexAttribType attribType = fVAPtr[index].fType;
            int slVecCount = GrSLTypeVectorCount(effectSLType);
            int attribVecCount = GrVertexAttribTypeVectorCount(attribType);
            if (slVecCount != attribVecCount ||
                (static_cast<GrSLType>(-1) != slTypes[index] && slTypes[index] != effectSLType)) {
                return false;
            }
            slTypes[index] = effectSLType;
            effectIndex++;
        }
        // Make sure all attributes are consumed and we were able to find everything
        SkASSERT(s.count() == effectIndex);
    }

    return true;
}

////////////////////////////////////////////////////////////////////////////////

static void validate_vertex_attribs(const GrVertexAttrib* attribs, int count, size_t stride) {
    // this works as long as we're 4 byte-aligned
#ifdef SK_DEBUG
    uint32_t overlapCheck = 0;
    SkASSERT(count <= GrDrawState::kMaxVertexAttribCnt);
    for (int index = 0; index < count; ++index) {
        size_t attribSize = GrVertexAttribTypeSize(attribs[index].fType);
        size_t attribOffset = attribs[index].fOffset;
        SkASSERT(attribOffset + attribSize <= stride);
        size_t dwordCount = attribSize >> 2;
        uint32_t mask = (1 << dwordCount)-1;
        size_t offsetShift = attribOffset >> 2;
        SkASSERT(!(overlapCheck & (mask << offsetShift)));
        overlapCheck |= (mask << offsetShift);
    }
#endif
}

////////////////////////////////////////////////////////////////////////////////

void GrDrawState::internalSetVertexAttribs(const GrVertexAttrib* attribs, int count,
                                           size_t stride) {
    SkASSERT(count <= kMaxVertexAttribCnt);

    fVAPtr = attribs;
    fVACount = count;
    fVAStride = stride;
    validate_vertex_attribs(fVAPtr, fVACount, fVAStride);

    // Set all the indices to -1
    memset(fFixedFunctionVertexAttribIndices,
           0xff,
           sizeof(fFixedFunctionVertexAttribIndices));
#ifdef SK_DEBUG
    uint32_t overlapCheck = 0;
#endif
    for (int i = 0; i < count; ++i) {
        if (attribs[i].fBinding < kGrFixedFunctionVertexAttribBindingCnt) {
            // The fixed function attribs can only be specified once
            SkASSERT(-1 == fFixedFunctionVertexAttribIndices[attribs[i].fBinding]);
            SkASSERT(GrFixedFunctionVertexAttribVectorCount(attribs[i].fBinding) ==
                     GrVertexAttribTypeVectorCount(attribs[i].fType));
            fFixedFunctionVertexAttribIndices[attribs[i].fBinding] = i;
        }
#ifdef SK_DEBUG
        size_t dwordCount = GrVertexAttribTypeSize(attribs[i].fType) >> 2;
        uint32_t mask = (1 << dwordCount)-1;
        size_t offsetShift = attribs[i].fOffset >> 2;
        SkASSERT(!(overlapCheck & (mask << offsetShift)));
        overlapCheck |= (mask << offsetShift);
#endif
    }
    fColorProcInfoValid = false;
    fCoverageProcInfoValid = false;
    // Positions must be specified.
    SkASSERT(-1 != fFixedFunctionVertexAttribIndices[kPosition_GrVertexAttribBinding]);
}

////////////////////////////////////////////////////////////////////////////////

void GrDrawState::setDefaultVertexAttribs() {
    static const GrVertexAttrib kPositionAttrib =
        {kVec2f_GrVertexAttribType, 0, kPosition_GrVertexAttribBinding};

    fVAPtr = &kPositionAttrib;
    fVACount = 1;
    fVAStride = GrVertexAttribTypeSize(kVec2f_GrVertexAttribType);

    // set all the fixed function indices to -1 except position.
    memset(fFixedFunctionVertexAttribIndices,
           0xff,
           sizeof(fFixedFunctionVertexAttribIndices));
    fFixedFunctionVertexAttribIndices[kPosition_GrVertexAttribBinding] = 0;
    fColorProcInfoValid = false;
    fCoverageProcInfoValid = false;
}

////////////////////////////////////////////////////////////////////////////////

bool GrDrawState::couldApplyCoverage(const GrDrawTargetCaps& caps) const {
    if (caps.dualSourceBlendingSupport()) {
        return true;
    }
    // we can correctly apply coverage if a) we have dual source blending
    // or b) one of our blend optimizations applies
    // or c) the src, dst blend coeffs are 1,0 and we will read Dst Color
    GrBlendCoeff srcCoeff;
    GrBlendCoeff dstCoeff;
    BlendOptFlags flag = this->getBlendOpts(true, &srcCoeff, &dstCoeff);
    return GrDrawState::kNone_BlendOpt != flag ||
           (this->willEffectReadDstColor() &&
            kOne_GrBlendCoeff == srcCoeff && kZero_GrBlendCoeff == dstCoeff);
}

bool GrDrawState::hasSolidCoverage() const {
    // If we're drawing coverage directly then coverage is effectively treated as color.
    if (this->isCoverageDrawing()) {
        return true;
    }

    if (this->numCoverageStages() > 0) {
        return false;
    }

    this->calcCoverageInvariantOutput();
    return fCoverageProcInfo.isSolidWhite();
}

//////////////////////////////////////////////////////////////////////////////

GrDrawState::AutoVertexAttribRestore::AutoVertexAttribRestore(GrDrawState* drawState) {
    SkASSERT(drawState);
    fDrawState = drawState;
    fVAPtr = drawState->fVAPtr;
    fVACount = drawState->fVACount;
    fVAStride = drawState->fVAStride;
    fDrawState->setDefaultVertexAttribs();
}

//////////////////////////////////////////////////////////////////////////////s

bool GrDrawState::willEffectReadDstColor() const {
    if (!this->isColorWriteDisabled()) {
        this->calcColorInvariantOutput();
        if (fColorProcInfo.readsDst()) {
            return true;
        }
    }
    this->calcCoverageInvariantOutput();
    return fCoverageProcInfo.readsDst();
}

void GrDrawState::AutoRestoreEffects::set(GrDrawState* ds) {
    if (fDrawState) {
        // See the big comment on the class definition about GPs.
        if (SK_InvalidUniqueID == fOriginalGPID) {
            fDrawState->fGeometryProcessor.reset(NULL);
        } else {
            SkASSERT(fDrawState->getGeometryProcessor()->getUniqueID() ==
                     fOriginalGPID);
            fOriginalGPID = SK_InvalidUniqueID;
        }

        int m = fDrawState->numColorStages() - fColorEffectCnt;
        SkASSERT(m >= 0);
        fDrawState->fColorStages.pop_back_n(m);

        int n = fDrawState->numCoverageStages() - fCoverageEffectCnt;
        SkASSERT(n >= 0);
        fDrawState->fCoverageStages.pop_back_n(n);
        if (m + n > 0) {
            fDrawState->fColorProcInfoValid = false;
            fDrawState->fCoverageProcInfoValid = false;
        }
        SkDEBUGCODE(--fDrawState->fBlockEffectRemovalCnt;)
    }
    fDrawState = ds;
    if (NULL != ds) {
        SkASSERT(SK_InvalidUniqueID == fOriginalGPID);
        if (NULL != ds->getGeometryProcessor()) {
            fOriginalGPID = ds->getGeometryProcessor()->getUniqueID();
        }
        fColorEffectCnt = ds->numColorStages();
        fCoverageEffectCnt = ds->numCoverageStages();
        SkDEBUGCODE(++ds->fBlockEffectRemovalCnt;)
    }
}

////////////////////////////////////////////////////////////////////////////////

// Some blend modes allow folding a fractional coverage value into the color's alpha channel, while
// others will blend incorrectly.
bool GrDrawState::canTweakAlphaForCoverage() const {
    /*
     The fractional coverage is f.
     The src and dst coeffs are Cs and Cd.
     The dst and src colors are S and D.
     We want the blend to compute: f*Cs*S + (f*Cd + (1-f))D. By tweaking the source color's alpha
     we're replacing S with S'=fS. It's obvious that that first term will always be ok. The second
     term can be rearranged as [1-(1-Cd)f]D. By substituting in the various possibilities for Cd we
     find that only 1, ISA, and ISC produce the correct destination when applied to S' and D.
     Also, if we're directly rendering coverage (isCoverageDrawing) then coverage is treated as
     color by definition.
     */
    return kOne_GrBlendCoeff == fDstBlend ||
           kISA_GrBlendCoeff == fDstBlend ||
           kISC_GrBlendCoeff == fDstBlend ||
           this->isCoverageDrawing();
}

////////////////////////////////////////////////////////////////////////////////

void GrDrawState::AutoViewMatrixRestore::restore() {
    if (fDrawState) {
        SkDEBUGCODE(--fDrawState->fBlockEffectRemovalCnt;)
        fDrawState->fViewMatrix = fViewMatrix;
        SkASSERT(fDrawState->numColorStages() >= fNumColorStages);
        int numCoverageStages = fSavedCoordChanges.count() - fNumColorStages;
        SkASSERT(fDrawState->numCoverageStages() >= numCoverageStages);

        int i = 0;
        for (int s = 0; s < fNumColorStages; ++s, ++i) {
            fDrawState->fColorStages[s].restoreCoordChange(fSavedCoordChanges[i]);
        }
        for (int s = 0; s < numCoverageStages; ++s, ++i) {
            fDrawState->fCoverageStages[s].restoreCoordChange(fSavedCoordChanges[i]);
        }
        fDrawState = NULL;
    }
}

void GrDrawState::AutoViewMatrixRestore::set(GrDrawState* drawState,
                                             const SkMatrix& preconcatMatrix) {
    this->restore();

    SkASSERT(NULL == fDrawState);
    if (NULL == drawState || preconcatMatrix.isIdentity()) {
        return;
    }
    fDrawState = drawState;

    fViewMatrix = drawState->getViewMatrix();
    drawState->fViewMatrix.preConcat(preconcatMatrix);

    this->doEffectCoordChanges(preconcatMatrix);
    SkDEBUGCODE(++fDrawState->fBlockEffectRemovalCnt;)
}

bool GrDrawState::AutoViewMatrixRestore::setIdentity(GrDrawState* drawState) {
    this->restore();

    if (NULL == drawState) {
        return false;
    }

    if (drawState->getViewMatrix().isIdentity()) {
        return true;
    }

    fViewMatrix = drawState->getViewMatrix();
    if (0 == drawState->numFragmentStages()) {
        drawState->fViewMatrix.reset();
        fDrawState = drawState;
        fNumColorStages = 0;
        fSavedCoordChanges.reset(0);
        SkDEBUGCODE(++fDrawState->fBlockEffectRemovalCnt;)
        return true;
    } else {
        SkMatrix inv;
        if (!fViewMatrix.invert(&inv)) {
            return false;
        }
        drawState->fViewMatrix.reset();
        fDrawState = drawState;
        this->doEffectCoordChanges(inv);
        SkDEBUGCODE(++fDrawState->fBlockEffectRemovalCnt;)
        return true;
    }
}

void GrDrawState::AutoViewMatrixRestore::doEffectCoordChanges(const SkMatrix& coordChangeMatrix) {
    fSavedCoordChanges.reset(fDrawState->numFragmentStages());
    int i = 0;

    fNumColorStages = fDrawState->numColorStages();
    for (int s = 0; s < fNumColorStages; ++s, ++i) {
        fDrawState->getColorStage(s).saveCoordChange(&fSavedCoordChanges[i]);
        fDrawState->fColorStages[s].localCoordChange(coordChangeMatrix);
    }

    int numCoverageStages = fDrawState->numCoverageStages();
    for (int s = 0; s < numCoverageStages; ++s, ++i) {
        fDrawState->getCoverageStage(s).saveCoordChange(&fSavedCoordChanges[i]);
        fDrawState->fCoverageStages[s].localCoordChange(coordChangeMatrix);
    }
}

////////////////////////////////////////////////////////////////////////////////

void GrDrawState::convertToPendingExec() {
    fRenderTarget.markPendingIO();
    fRenderTarget.removeRef();
    for (int i = 0; i < fColorStages.count(); ++i) {
        fColorStages[i].convertToPendingExec();
    }
    if (fGeometryProcessor) {
        fGeometryProcessor.convertToPendingExec();
    }
    for (int i = 0; i < fCoverageStages.count(); ++i) {
        fCoverageStages[i].convertToPendingExec();
    }
}

////////////////////////////////////////////////////////////////////////////////

GrDrawState::~GrDrawState() {
    SkASSERT(0 == fBlockEffectRemovalCnt);
}

////////////////////////////////////////////////////////////////////////////////

GrDrawState::BlendOptFlags GrDrawState::getBlendOpts(bool forceCoverage,
                                                     GrBlendCoeff* srcCoeff,
                                                     GrBlendCoeff* dstCoeff) const {
    GrBlendCoeff bogusSrcCoeff, bogusDstCoeff;
    if (NULL == srcCoeff) {
        srcCoeff = &bogusSrcCoeff;
    }
    if (NULL == dstCoeff) {
        dstCoeff = &bogusDstCoeff;
    }

    *srcCoeff = this->getSrcBlendCoeff();
    *dstCoeff = this->getDstBlendCoeff();

    if (this->isColorWriteDisabled()) {
        *srcCoeff = kZero_GrBlendCoeff;
        *dstCoeff = kOne_GrBlendCoeff;
    }

    bool srcAIsOne = this->srcAlphaWillBeOne();
    bool dstCoeffIsOne = kOne_GrBlendCoeff == *dstCoeff ||
                         (kSA_GrBlendCoeff == *dstCoeff && srcAIsOne);
    bool dstCoeffIsZero = kZero_GrBlendCoeff == *dstCoeff ||
                         (kISA_GrBlendCoeff == *dstCoeff && srcAIsOne);

    // When coeffs are (0,1) there is no reason to draw at all, unless
    // stenciling is enabled. Having color writes disabled is effectively
    // (0,1).
    if ((kZero_GrBlendCoeff == *srcCoeff && dstCoeffIsOne)) {
        if (this->getStencil().doesWrite()) {
            return kEmitCoverage_BlendOptFlag;
        } else {
            *dstCoeff = kOne_GrBlendCoeff;
            return kSkipDraw_BlendOptFlag;
        }
    }

    bool hasCoverage = forceCoverage || !this->hasSolidCoverage();

    // if we don't have coverage we can check whether the dst
    // has to read at all. If not, we'll disable blending.
    if (!hasCoverage) {
        if (dstCoeffIsZero) {
            if (kOne_GrBlendCoeff == *srcCoeff) {
                // if there is no coverage and coeffs are (1,0) then we
                // won't need to read the dst at all, it gets replaced by src
                *dstCoeff = kZero_GrBlendCoeff;
                return kNone_BlendOpt;
            } else if (kZero_GrBlendCoeff == *srcCoeff) {
                // if the op is "clear" then we don't need to emit a color
                // or blend, just write transparent black into the dst.
                *srcCoeff = kOne_GrBlendCoeff;
                *dstCoeff = kZero_GrBlendCoeff;
                return kEmitTransBlack_BlendOptFlag;
            }
        }
    } else if (this->isCoverageDrawing()) {
        // we have coverage but we aren't distinguishing it from alpha by request.
        return kCoverageAsAlpha_BlendOptFlag;
    } else {
        // check whether coverage can be safely rolled into alpha
        // of if we can skip color computation and just emit coverage
        if (this->canTweakAlphaForCoverage()) {
            return kCoverageAsAlpha_BlendOptFlag;
        }
        if (dstCoeffIsZero) {
            if (kZero_GrBlendCoeff == *srcCoeff) {
                // the source color is not included in the blend
                // the dst coeff is effectively zero so blend works out to:
                // (c)(0)D + (1-c)D = (1-c)D.
                *dstCoeff = kISA_GrBlendCoeff;
                return  kEmitCoverage_BlendOptFlag;
            } else if (srcAIsOne) {
                // the dst coeff is effectively zero so blend works out to:
                // cS + (c)(0)D + (1-c)D = cS + (1-c)D.
                // If Sa is 1 then we can replace Sa with c
                // and set dst coeff to 1-Sa.
                *dstCoeff = kISA_GrBlendCoeff;
                return  kCoverageAsAlpha_BlendOptFlag;
            }
        } else if (dstCoeffIsOne) {
            // the dst coeff is effectively one so blend works out to:
            // cS + (c)(1)D + (1-c)D = cS + D.
            *dstCoeff = kOne_GrBlendCoeff;
            return  kCoverageAsAlpha_BlendOptFlag;
        }
    }

    return kNone_BlendOpt;
}

bool GrDrawState::srcAlphaWillBeOne() const {
    this->calcColorInvariantOutput();
    if (this->isCoverageDrawing()) {
        this->calcCoverageInvariantOutput();
        return (fColorProcInfo.isOpaque() && fCoverageProcInfo.isOpaque());
    }
    return fColorProcInfo.isOpaque();
}

bool GrDrawState::willBlendWithDst() const {
    if (!this->hasSolidCoverage()) {
        return true;
    }

    if (this->willEffectReadDstColor()) {
        return true;
    }

    if (GrBlendCoeffRefsDst(this->getSrcBlendCoeff())) {
        return true;
    }

    GrBlendCoeff dstCoeff = this->getDstBlendCoeff();
    if (!(kZero_GrBlendCoeff == dstCoeff ||
         (kISA_GrBlendCoeff == dstCoeff && this->srcAlphaWillBeOne()))) {
        return true;
    }

    return false;
}

void GrDrawState::calcColorInvariantOutput() const {
    if (!fColorProcInfoValid) {
        GrColor color;
        GrColorComponentFlags flags;
        if (this->hasColorVertexAttribute()) {
            if (fHints & kVertexColorsAreOpaque_Hint) {
                flags = kA_GrColorComponentFlag;
                color = 0xFF << GrColor_SHIFT_A;
            } else {
                flags = static_cast<GrColorComponentFlags>(0);
                color = 0;
            }
        } else {
            flags = kRGBA_GrColorComponentFlags;
            color = this->getColor();
        }
        fColorProcInfo.calcWithInitialValues(fColorStages.begin(), this->numColorStages(),
                                             color, flags, false);
        fColorProcInfoValid = true;
    }
}

void GrDrawState::calcCoverageInvariantOutput() const {
    if (!fCoverageProcInfoValid) {
        GrColor color;
        GrColorComponentFlags flags;
        // Check if per-vertex or constant color may have partial alpha
        if (this->hasCoverageVertexAttribute()) {
            flags = static_cast<GrColorComponentFlags>(0);
            color = 0;
        } else {
            flags = kRGBA_GrColorComponentFlags;
            color = this->getCoverageColor();
        }
        fCoverageProcInfo.calcWithInitialValues(fCoverageStages.begin(), this->numCoverageStages(),
                                                color, flags, true, fGeometryProcessor.get());
        fCoverageProcInfoValid = true;
    }
}
