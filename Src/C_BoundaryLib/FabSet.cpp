//BL_COPYRIGHT_NOTICE

//
// $Id: FabSet.cpp,v 1.9 1998-04-27 19:43:19 lijewski Exp $
//

#include <FabSet.H>
#include <Looping.H>

FabSet::FabSet () {}

FabSet::~FabSet () {}

FabSet::FabSet (int _len)
{
    fabparray.resize(_len);
}

void
FabSet::setFab (int        boxno,
                FArrayBox* fab)
{
    if (n_comp == 0)
    {
        n_comp = fab->nComp();
    }
    assert(n_comp == fab->nComp());
    assert(boxarray.ready());
    assert(!fabparray.defined(boxno));
    assert(distributionMap.ProcessorMap()[boxno] == ParallelDescriptor::MyProc());

    fabparray.set(boxno, fab);

    fabboxarray.convert(fab->box().ixType());
    fabboxarray.set(boxno, fab->box());
}

FabSet&
FabSet::copyFrom (const FArrayBox& src)
{
    for (FabSetIterator fsi(*this); fsi.isValid(false); ++fsi)
    {
        fsi().copy(src);
    }
    return *this;
}

FabSet&
FabSet::copyFrom (const FArrayBox& src,
                  int              src_comp,
                  int              dest_comp,
                  int              num_comp)
{
    for (FabSetIterator fsi(*this); fsi.isValid(false); ++fsi)
    {
        fsi().copy(src,src_comp,dest_comp,num_comp);
    }
    return *this;
}

FabSet&
FabSet::copyFrom (const FArrayBox& src,
                  const Box&       subbox,
                  int              src_comp,
                  int              dest_comp,
                  int              num_comp)
{
    assert(src.box().contains(subbox));

    for (FabSetIterator fsi(*this); fsi.isValid(false); ++fsi)
    {
        Box dbox = fsi().box() & subbox;

        if (dbox.ok())
        {
            fsi().copy(src,dbox,src_comp,dbox,dest_comp,num_comp);
        }
    }
    return *this;
}

FabSet&
FabSet::copyFrom (const MultiFab& src,
                  int             nghost,
                  int             src_comp,
                  int             dest_comp,
                  int             num_comp)
{
    assert(nghost <= src.nGrow());

    FabSetCopyDescriptor fscd;

    MultiFabId srcmfid = fscd.RegisterFabArray(const_cast<MultiFab*>(&src));

    vector<FillBoxId> fillBoxIdList;

    const BoxArray& sba = src.boxArray();

    for (FabSetIterator fsi(*this); fsi.isValid(false); ++fsi)
    {
       for (int s = 0; s < src.length(); ++s)
       {
           Box ovlp = fsi().box() & ::grow(sba[s],nghost);

            if (ovlp.ok())
            {
                fillBoxIdList.push_back(fscd.AddBox(srcmfid,
                                                    src.fabbox(s),
                                                    0,
                                                    src_comp,
                                                    dest_comp,
                                                    num_comp,
                                                    false));
            }
        }
    }

    fscd.CollectData();

    vector<FillBoxId>::const_iterator fbidli = fillBoxIdList.begin();

    for (FabSetIterator fsi(*this); fsi.isValid(false); ++fsi)
    {
        FArrayBox& dfab = fsi();

        for (int s = 0; s < src.length(); ++s)
        {
            Box ovlp = dfab.box() & ::grow(sba[s],nghost);

            if (ovlp.ok())
            {
                assert(!(fbidli == fillBoxIdList.end()));
                FillBoxId fbid = *fbidli++;
                FArrayBox sfabTemp(fbid.box(), num_comp);
                fscd.FillFab(srcmfid, fbid, sfabTemp);
                dfab.copy(sfabTemp, ovlp, 0, ovlp, dest_comp, num_comp);
            }
        }
    }

    return *this;
}

FabSet&
FabSet::plusFrom (const MultiFab& src,
                  int             nghost,
                  int             src_comp,
                  int             dest_comp,
                  int             num_comp)
{
    //
    // This can be optimized by only communicating the components used in
    // the linComp.  This implementation communicates all the components.
    //
    assert(nghost <= src.nGrow());

    const BoxArray& sba = src.boxArray();

    MultiFabCopyDescriptor mfcd;
    MultiFabId             mfidsrc = mfcd.RegisterFabArray((MultiFab *) &src);
    vector<FillBoxId>      fillBoxIdList;

    for (FabSetIterator thismfi(*this); thismfi.isValid(false); ++thismfi)
    {
        for (int isrc = 0; isrc < src.length(); ++isrc)
        {
            Box ovlp = thismfi().box() & ::grow(sba[isrc], nghost);

            if (ovlp.ok())
            {
                fillBoxIdList.push_back(mfcd.AddBox(mfidsrc,
                                                    ovlp,
                                                    0,
                                                    0,
                                                    0,
                                                    num_comp));
            }
        }
    }

    mfcd.CollectData();

    vector<FillBoxId>::const_iterator fbidli = fillBoxIdList.begin();

    for (FabSetIterator thismfi(*this); thismfi.isValid(false); ++thismfi)
    {
        FArrayBox& dfab = thismfi();

        for (int isrc = 0; isrc < src.length(); ++isrc)
        {
            Box ovlp = dfab.box() & ::grow(sba[isrc], nghost);

            if (ovlp.ok())
            {
                assert(!(fbidli == fillBoxIdList.end()));
                FillBoxId fbidsrc = *fbidli++;
                FArrayBox sfab(fbidsrc.box(), num_comp);
                mfcd.FillFab(mfidsrc, fbidsrc, sfab);
                dfab.plus(sfab,ovlp,src_comp,dest_comp,num_comp);
            }
        }
    }

    return *this;
}

//
// Linear combination this := a*this + b*src
// Note: corresponding fabsets must be commensurate.
//
FabSet&
FabSet::linComb (Real          a,
                 Real          b,
                 const FabSet& src,
                 int           src_comp,
                 int           dest_comp,
                 int           num_comp)
{
    assert(length() == src.length());

    for (FabSetIterator fsi(*this); fsi.isValid(false); ++fsi)
    {
        DependentFabSetIterator dfsi(fsi, src);

        FArrayBox&       dfab = fsi();
        const FArrayBox& sfab = dfsi();
        const Box&       dbox = dfab.box();
        const Box&       sbox = sfab.box();

        assert(dbox == sbox);
        //
        // WARNING: same fab used as src and dest here.
        //
        dfab.linComb(dfab,
                     dbox,
                     dest_comp,
                     sfab,
                     sbox,
                     src_comp,
                     a,
                     b,
                     dbox,
                     dest_comp,
                     num_comp);
    }
    return *this;
}

FabSet&
FabSet::linComb (Real            a,
                 const MultiFab& mfa,
                 int             a_comp,
                 Real            b,
                 const MultiFab& mfb,
                 int             b_comp,
                 int             dest_comp,
                 int             num_comp,
                 int             n_ghost)
{
    const BoxArray& bxa = mfa.boxArray();
    const BoxArray& bxb = mfb.boxArray();

    assert(bxa == bxb);
    assert(n_ghost <= mfa.nGrow());
    assert(n_ghost <= mfb.nGrow());
    //
    // This can be optimized by only communicating the components used in
    // the linComp.  This implementation communicates all the components.
    //
    MultiFabCopyDescriptor mfcd;

    MultiFabId mfid_mfa = mfcd.RegisterFabArray((MultiFab *) &mfa);
    MultiFabId mfid_mfb = mfcd.RegisterFabArray((MultiFab *) &mfb);

    vector<FillBoxId> fillBoxIdList_mfa;
    vector<FillBoxId> fillBoxIdList_mfb;

    for (FabSetIterator thismfi(*this); thismfi.isValid(false); ++thismfi)
    {
        FArrayBox& reg_fab = thismfi();

        for (int grd = 0; grd < bxa.length(); grd++)
        {
            Box ovlp = reg_fab.box() & ::grow(bxa[grd],n_ghost);

            if (ovlp.ok())
            {
                fillBoxIdList_mfa.push_back(mfcd.AddBox(mfid_mfa,
                                                        mfa.fabbox(grd),
                                                        0,
                                                        0,
                                                        0,
                                                        num_comp,
                                                        false));
                fillBoxIdList_mfb.push_back(mfcd.AddBox(mfid_mfb,
                                                        mfb.fabbox(grd),
                                                        0,
                                                        0,
                                                        0,
                                                        num_comp,
                                                        false));
            }
        }
    }

    mfcd.CollectData();

    vector<FillBoxId>::const_iterator fbidli_mfa = fillBoxIdList_mfa.begin();
    vector<FillBoxId>::const_iterator fbidli_mfb = fillBoxIdList_mfb.begin();

    for (FabSetIterator thismfi(*this); thismfi.isValid(false); ++thismfi)
    {
        FArrayBox& reg_fab = thismfi();

        for (int grd = 0; grd < bxa.length(); grd++)
        {
            Box ovlp = reg_fab.box() & ::grow(bxa[grd],n_ghost);

            if (ovlp.ok())
            {
                assert(!(fbidli_mfa == fillBoxIdList_mfa.end()));
                FillBoxId fbid_mfa = *fbidli_mfa++;
                FArrayBox a_fab(fbid_mfa.box(), num_comp);
                mfcd.FillFab(mfid_mfa, fbid_mfa, a_fab);

                assert(!(fbidli_mfb == fillBoxIdList_mfb.end()));
                FillBoxId fbid_mfb = *fbidli_mfb++;
                FArrayBox b_fab(fbid_mfb.box(), num_comp);
                mfcd.FillFab(mfid_mfb, fbid_mfb, b_fab);

                reg_fab.linComb(a_fab,
                                ovlp,
                                a_comp,
                                b_fab,
                                ovlp,
                                b_comp,
                                a,
                                b,
                                ovlp,
                                dest_comp,
                                num_comp);
            }
        }
    }

    return *this;
}
