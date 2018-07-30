/******************************************************************************
 * Copyright © 2014-2018 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/

#include "CCdice.h"

/*
 in order to implement a dice game, we need a source of entropy, reasonably fast completion time and a way to manage the utxos.
 
 1. CC vout locks "house" funds with hash(entropy) + half of shared secret
 2. bettor submits bet, with entropy, odds, houseid and sends combined amount into another CC vout.
 3. house account sends funds to winner with proof of entropy
 4. if timeout, bettor wins funds
 
 2. and 3. can be done in mempool
 */

#include "../endian.h"

void endiancpy(uint8_t *dest,uint8_t *src,int32_t len)
{
    int32_t i,j=0;
#if defined(WORDS_BIGENDIAN)
    for (i=31; i>=0; i--)
        dest[j++] = src[i];
#else
    memcpy(dest,src,len);
#endif
}

uint256 DiceHashEntropy(uint256 &entropy,uint256 _txidpriv) // max 1 vout per txid used
{
    int32_t i; uint8_t _entropy[32],_hentropy[32]; bits256 tmp256,txidpub,txidpriv,mypriv,mypub,ssecret,ssecret2; uint256 hentropy;
    memset(&hentropy,0,32);
    endiancpy(txidpriv.bytes,(uint8_t *)&_txidpriv,32);
    txidpriv.bytes[0] &= 0xf8, txidpriv.bytes[31] &= 0x7f, txidpriv.bytes[31] |= 0x40;
    txidpub = curve25519(txidpriv,curve25519_basepoint9());

    Myprivkey(tmp256.bytes);
    vcalc_sha256(0,mypriv.bytes,tmp256.bytes,32);
    mypriv.bytes[0] &= 0xf8, mypriv.bytes[31] &= 0x7f, mypriv.bytes[31] |= 0x40;
    mypub = curve25519(mypriv,curve25519_basepoint9());

    ssecret = curve25519(mypriv,txidpub);
    ssecret2 = curve25519(txidpriv,mypub);
    if ( memcmp(ssecret.bytes,ssecret2.bytes,32) == 0 )
    {
        vcalc_sha256(0,(uint8_t *)&_entropy,ssecret.bytes,32);
        vcalc_sha256(0,(uint8_t *)&_hentropy,_entropy,32);
        endiancpy((uint8_t *)&entropy,_entropy,32);
        endiancpy((uint8_t *)&hentropy,_hentropy,32);
    }
    else
    {
        for (i=0; i<32; i++)
            fprintf(stderr,"%02x",ssecret.bytes[i]);
        fprintf(stderr," ssecret\n");
        for (i=0; i<32; i++)
            fprintf(stderr,"%02x",ssecret2.bytes[i]);
        fprintf(stderr," ssecret2 dont match\n");
    }
    return(hentropy);
}

uint64_t DiceCalc(int64_t bet,int64_t odds,int64_t minbet,int64_t maxbet,int64_t maxodds,int64_t forfeitblocks,uint256 houseentropy,uint256 bettorentropy)
{
    uint8_t buf[64],_house[32],_bettor[32]; uint64_t winnings; arith_uint256 house,bettor; char str[65],str2[65];
    if ( odds < 10000 )
        return(0);
    else odds -= 10000;
    if ( bet < minbet || bet > maxbet || odds > maxodds )
    {
        fprintf(stderr,"bet size violation %.8f\n",(double)bet/COIN);
        return(0);
    }
    endiancpy(buf,(uint8_t *)&houseentropy,32);
    endiancpy(&buf[32],(uint8_t *)&bettorentropy,32);
    vcalc_sha256(0,(uint8_t *)&_house,buf,64);
    endiancpy((uint8_t *)&house,_house,32);

    endiancpy(buf,(uint8_t *)&bettorentropy,32);
    endiancpy(&buf[32],(uint8_t *)&houseentropy,32);
    vcalc_sha256(0,(uint8_t *)&_house,buf,64);
    endiancpy((uint8_t *)&bettor,_bettor,32);
    if ( odds > 1 )
        bettor = (bettor / arith_uint256(odds));
    if ( bettor >= house )
        winnings = bet * odds;
    else winnings = 0;
    fprintf(stderr,"bet %.8f at odds %d:1 %s vs %s\n",(double)bet/COIN,(int32_t)odds,uint256_str(str,*(uint256 *)&house),uint256_str(str2,*(uint256 *)&bettor));
    return(0);
}

CScript EncodeDiceFundingOpRet(uint8_t funcid,uint64_t sbits,int64_t minbet,int64_t maxbet,int64_t maxodds,int64_t forfeitblocks)
{
    CScript opret; uint8_t evalcode = EVAL_DICE;
    opret << OP_RETURN << E_MARSHAL(ss << evalcode << 'F' << sbits << minbet << maxbet << maxodds << forfeitblocks);
    return(opret);
}

uint8_t DecodeDiceFundingOpRet(const CScript &scriptPubKey,uint64_t &sbits,int64_t &minbet,int64_t &maxbet,int64_t &maxodds,int64_t &forfeitblocks)
{
    std::vector<uint8_t> vopret; uint8_t *script,e,f;
    GetOpReturnData(scriptPubKey, vopret);
    script = (uint8_t *)vopret.data();
    if ( vopret.size() > 2 && E_UNMARSHAL(vopret,ss >> e; ss >> f; ss >> sbits; ss >> minbet; ss >> maxbet; ss >> maxodds; ss >> forfeitblocks) != 0 )
    {
        if ( e == EVAL_DICE && f == 'F' )
            return(f);
    }
    return(0);
}

CScript EncodeDiceOpRet(uint8_t funcid,uint64_t sbits,uint256 fundingtxid,uint256 hash)
{
    CScript opret; uint8_t evalcode = EVAL_DICE;
    opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << sbits << fundingtxid << hash);
    return(opret);
}

uint8_t DecodeDiceOpRet(uint256 txid,const CScript &scriptPubKey,uint64_t &sbits,uint256 &fundingtxid,uint256 &hash)
{
    std::vector<uint8_t> vopret; uint8_t *script,e,f,funcid; int64_t minbet,maxbet,maxodds,forfeitblocks;
    GetOpReturnData(scriptPubKey, vopret);
    if ( vopret.size() > 2 )
    {
        script = (uint8_t *)vopret.data();
        if ( script[0] == EVAL_DICE )
        {
            if ( script[1] == 'F' )
            {
                if ( E_UNMARSHAL(vopret,ss >> e; ss >> f; ss >> sbits; ss >> minbet; ss >> maxbet; ss >> maxodds; ss >> forfeitblocks) != 0 )
                {
                    memset(&hash,0,32);
                    fundingtxid = txid;
                    return('F');
                } else fprintf(stderr,"unmarshal error for F\n");
            }
            else if ( E_UNMARSHAL(vopret,ss >> e; ss >> f; ss >> sbits; ss >> fundingtxid; ss >> hash) != 0 )
            {
                if ( e == EVAL_DICE && (f == 'B' || f == 'U' || f == 'E') )
                    return(f);
                else fprintf(stderr,"mismatched e.%02x f.(%c)\n",e,f);
            }
        } else fprintf(stderr,"script[0] %02x != EVAL_DICE\n",script[0]);
    } else fprintf(stderr,"not enough opret.[%d]\n",(int32_t)vopret.size());
    return(0);
}

uint64_t IsDicevout(struct CCcontract_info *cp,const CTransaction& tx,int32_t v)
{
    char destaddr[64];
    if ( tx.vout[v].scriptPubKey.IsPayToCryptoCondition() != 0 )
    {
        if ( Getscriptaddress(destaddr,tx.vout[v].scriptPubKey) > 0 && strcmp(destaddr,cp->unspendableCCaddr) == 0 )
            return(tx.vout[v].nValue);
    }
    return(0);
}

bool DiceExactAmounts(struct CCcontract_info *cp,Eval *eval,const CTransaction &tx,uint64_t txfee)
{
    static uint256 zerohash;
    CTransaction vinTx; uint256 hashBlock; int32_t i,numvins,numvouts; uint64_t inputs=0,outputs=0,assetoshis;
    numvins = tx.vin.size();
    numvouts = tx.vout.size();
    for (i=0; i<numvins; i++)
    {
        if ( (*cp->ismyvin)(tx.vin[i].scriptSig) != 0 )
        {
            if ( eval->GetTxUnconfirmed(tx.vin[i].prevout.hash,vinTx,hashBlock) == 0 )
                return eval->Invalid("always should find vin, but didnt");
            else
            {
                if ( hashBlock == zerohash )
                    return eval->Invalid("cant dice from mempool");
                if ( (assetoshis= IsDicevout(cp,vinTx,tx.vin[i].prevout.n)) != 0 )
                    inputs += assetoshis;
            }
        }
    }
    for (i=0; i<numvouts; i++)
    {
        //fprintf(stderr,"i.%d of numvouts.%d\n",i,numvouts);
        if ( (assetoshis= IsDicevout(cp,tx,i)) != 0 )
            outputs += assetoshis;
    }
    if ( inputs != outputs+txfee )
    {
        fprintf(stderr,"inputs %llu vs outputs %llu\n",(long long)inputs,(long long)outputs);
        return eval->Invalid("mismatched inputs != outputs + txfee");
    }
    else return(true);
}

bool DiceIsmine(const CScript scriptPubKey)
{
    char destaddr[64],myaddr[64];
    Getscriptaddress(destaddr,scriptPubKey);
    Getscriptaddress(myaddr,CScript() << Mypubkey() << OP_CHECKSIG);
    return(strcmp(destaddr,myaddr) == 0);
}

bool DiceValidate(struct CCcontract_info *cp,Eval* eval,const CTransaction &tx)
{
    uint256 txid,fundingtxid,hashBlock,hash; int64_t minbet,maxbet,maxodds,forfeitblocks; uint64_t sbits,amount,reward,txfee=10000; int32_t numvins,numvouts,preventCCvins,preventCCvouts,i; uint8_t funcid; CScript scriptPubKey; CTransaction fundingTx,vinTx;
    numvins = tx.vin.size();
    numvouts = tx.vout.size();
    preventCCvins = preventCCvouts = -1;
    if ( numvouts < 1 )
        return eval->Invalid("no vouts");
    else
    {
        txid = tx.GetHash();
        if ( (funcid=  DecodeDiceOpRet(txid,tx.vout[numvouts-1].scriptPubKey,sbits,fundingtxid,hash)) != 0 )
        {
            if ( eval->GetTxUnconfirmed(fundingtxid,fundingTx,hashBlock) == 0 )
                return eval->Invalid("cant find fundingtxid");
            else if ( fundingTx.vout.size() > 0 && DecodeDiceFundingOpRet(fundingTx.vout[fundingTx.vout.size()-1].scriptPubKey,sbits,minbet,maxbet,maxodds,forfeitblocks) != 'F' )
                return eval->Invalid("fundingTx not valid");
            switch ( funcid )
            {
                case 'F':
                    //vins.*: normal inputs
                    //vout.0: CC vout for funding
                    //vout.1: normal marker vout for easy searching
                    //vout.2: normal change
                    //vout.n-1: opreturn 'F' sbits APR minseconds maxseconds mindeposit
                    return eval->Invalid("unexpected DiceValidate for createfunding");
                    break;
                case 'E':
                    //vins.*: normal inputs
                    //vout.0: CC vout for locked entropy funds
                    //vout.1: tag to owner address for entropy funds
                    //vout.2: normal change
                    //vout.n-1: opreturn 'E' sbits fundingtxid hentropy
                    return eval->Invalid("unexpected DiceValidate for addfunding entropy");
                    break;
                case 'B':
                    //vin.0: entropy txid from house
                    //vins.1+: normal inputs
                    //vout.0: CC vout for locked entropy
                    //vout.1: CC vout for locked bet
                    //vout.2: tag for bettor's address (txfee + odds)
                    //vout.3: change
                    //vout.n-1: opreturn 'B' sbits fundingtxid entropy
                    // get house hentropy and its vin0.prevtxid, cmp vout1 to owner address
                    // if owneraddress is me, then validate hentropy and submit outcome tx
                    fprintf(stderr,"got bet txid\n");
                    if ( eval->GetTxUnconfirmed(tx.vin[0].prevout.hash,vinTx,hashBlock) == 0 )
                        return eval->Invalid("always should find vin.0, but didnt");
                    else if ( DiceIsmine(vinTx.vout[1].scriptPubKey) != 0 && vinTx.vout.size() > 0 )
                    {
                        uint64_t vinsbits,winnings; uint256 vinfundingtxid,hentropy,hentropy2,entropy; char str[65],str2[65];
                        if ( DecodeDiceOpRet(txid,vinTx.vout[vinTx.vout.size()-1].scriptPubKey,vinsbits,vinfundingtxid,hentropy) == 'E' && sbits == vinsbits && fundingtxid == vinfundingtxid )
                        {
                            hentropy2 = DiceHashEntropy(entropy,vinTx.vin[0].prevout.hash);
                            if ( hentropy == hentropy2 )
                            {
                                winnings = DiceCalc(tx.vout[1].nValue,tx.vout[2].nValue,minbet,maxbet,maxodds,forfeitblocks,entropy,hash);
                                //fprintf(stderr,"I am house entropy %.8f entropy.(%s) vs %s -> winnings %.8f\n",(double)vinTx.vout[0].nValue/COIN,uint256_str(str,entropy),uint256_str(str2,hash),(double)winnings/COIN);
                                if ( winnings == 0 )
                                {
                                    // queue 'L' losing tx
                                }
                                else
                                {
                                    // queue 'W' winning tx
                                }
                            }
                        }
                    }
                    return eval->Invalid("dont confirm bet during debug");
                    break;
                case 'L':
                    break;
                case 'W':
                    break;
                case 'U':
                    //vin.0: locked funds CC vout.0 from lock
                    //vin.1+: funding CC vout.0 from 'F' and 'E' and 'U'
                    //vout.0: funding CC change
                    //vout.1: normal output to unlock address
                    //vout.n-1: opreturn 'U' sbits fundingtxid
                    for (i=0; i<numvins; i++)
                    {
                        if ( (*cp->ismyvin)(tx.vin[i].scriptSig) == 0 )
                            return eval->Invalid("unexpected normal vin for unlock");
                    }
                    if ( DiceExactAmounts(cp,eval,tx,txfee+tx.vout[1].nValue) == 0 )
                        return false;
                    else if ( eval->GetTxUnconfirmed(tx.vin[0].prevout.hash,vinTx,hashBlock) == 0 )
                        return eval->Invalid("always should find vin.0, but didnt");
                    else if ( vinTx.vout[0].scriptPubKey.IsPayToCryptoCondition() == 0 )
                        return eval->Invalid("lock tx vout.0 is normal output");
                    else if ( tx.vout.size() < 3 )
                        return eval->Invalid("unlock tx not enough vouts");
                    else if ( tx.vout[0].scriptPubKey.IsPayToCryptoCondition() == 0 )
                        return eval->Invalid("unlock tx vout.0 is normal output");
                    else if ( tx.vout[1].scriptPubKey.IsPayToCryptoCondition() != 0 )
                        return eval->Invalid("unlock tx vout.1 is CC output");
                    else if ( tx.vout[1].scriptPubKey != vinTx.vout[1].scriptPubKey )
                        return eval->Invalid("unlock tx vout.1 mismatched scriptPubKey");
                    amount = vinTx.vout[0].nValue;
                    reward = 0;
                    //reward = DiceCalc(amount,tx.vin[0].prevout.hash,minbet,maxbet,maxodds,forfeitblocks);
                    if ( tx.vout[1].nValue > amount+reward )
                        return eval->Invalid("unlock tx vout.1 isnt amount+reward");
                    preventCCvouts = 1;
                    break;
            }
        }
        return(PreventCC(eval,tx,preventCCvins,numvins,preventCCvouts,numvouts));
    }
    return(true);
}

// 'B' vs 'F' and 'E'
uint64_t AddDiceInputs(CScript &scriptPubKey,int32_t fundsflag,struct CCcontract_info *cp,CMutableTransaction &mtx,CPubKey pk,uint64_t total,int32_t maxinputs)
{
    char coinaddr[64],str[65]; uint64_t sbits,nValue,totalinputs = 0; uint256 txid,hash,hashBlock,fundingtxid; CTransaction tx; int32_t j,vout,n = 0; uint8_t funcid;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;
    GetCCaddress(cp,coinaddr,pk);
    SetCCunspents(unspentOutputs,coinaddr);
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++)
    {
        txid = it->first.txhash;
        vout = (int32_t)it->first.index;
        fprintf(stderr,"(%s) %s/v%d %.8f\n",coinaddr,uint256_str(str,txid),vout,(double)it->second.satoshis/COIN);
        for (j=0; j<mtx.vin.size(); j++)
            if ( txid == mtx.vin[j].prevout.hash && vout == mtx.vin[j].prevout.n )
                break;
        if ( j != mtx.vin.size() )
            continue;
        if ( GetTransaction(txid,tx,hashBlock,false) != 0 && tx.vout.size() > 0 && tx.vout[vout].scriptPubKey.IsPayToCryptoCondition() != 0 )
        {
            if ( (funcid= DecodeDiceOpRet(txid,tx.vout[tx.vout.size()-1].scriptPubKey,sbits,fundingtxid,hash)) != 0 )
            {
                fprintf(stderr,"fundsflag.%d (%c) %.8f %.8f\n",fundsflag,funcid,(double)tx.vout[vout].nValue/COIN,(double)it->second.satoshis/COIN);
                if ( fundsflag != 0 && funcid != 'F' && funcid != 'E' && funcid != 'U' )
                    continue;
                else if ( fundsflag == 0 && (funcid != 'B' || tx.vout.size() < 4) )
                    continue;
                if ( total != 0 && maxinputs != 0 )
                {
                    if ( fundsflag == 0 )
                        scriptPubKey = tx.vout[1].scriptPubKey;
                    mtx.vin.push_back(CTxIn(txid,vout,CScript()));
                }
                totalinputs += it->second.satoshis;
                n++;
                if ( (total > 0 && totalinputs >= total) || (maxinputs > 0 && n >= maxinputs) )
                    break;
            } else fprintf(stderr,"null funcid\n");
        }
    }
    return(totalinputs);
}

uint64_t DicePlanFunds(uint64_t &entropyval,uint256 &entropytxid,uint64_t refsbits,struct CCcontract_info *cp,CPubKey pk,uint256 reffundingtxid)
{
    char coinaddr[64]; uint64_t sbits,nValue,totalinputs = 0; uint256 hash,txid,hashBlock,fundingtxid; CTransaction tx; int32_t vout,first=0; uint8_t funcid;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;
    GetCCaddress(cp,coinaddr,pk);
    SetCCunspents(unspentOutputs,coinaddr);
    entropyval = 0;
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++)
    {
        txid = it->first.txhash;
        vout = (int32_t)it->first.index;
        if ( GetTransaction(txid,tx,hashBlock,false) != 0 && tx.vout[vout].scriptPubKey.IsPayToCryptoCondition() != 0 )
        {
            if ( (funcid= DecodeDiceOpRet(txid,tx.vout[tx.vout.size()-1].scriptPubKey,sbits,fundingtxid,hash)) != 0 )
            {
                if ( (funcid == 'F' && reffundingtxid == txid) || reffundingtxid == fundingtxid )
                {
                    if ( refsbits == sbits && (nValue= IsDicevout(cp,tx,vout)) > 0 )
                    {
                        totalinputs += nValue;
                        fprintf(stderr,"add %.8f\n",(double)nValue/COIN);
                        if ( first == 0 && funcid == 'E' )
                        {
                            entropytxid = txid;
                            entropyval = tx.vout[0].nValue;
                            first = 1;
                        }
                    }
                    else fprintf(stderr,"refsbits.%llx sbits.%llx nValue %.8f\n",(long long)refsbits,(long long)sbits,(double)nValue/COIN);
                } else fprintf(stderr,"else case funcid %d\n",funcid);
            } else fprintf(stderr,"funcid.%d %c skipped %.8f\n",funcid,funcid,(double)tx.vout[vout].nValue/COIN);
        }
    }
    return(totalinputs);
}

bool DicePlanExists(struct CCcontract_info *cp,uint64_t refsbits,CPubKey dicepk,int64_t &minbet,int64_t &maxbet,int64_t &maxodds,int64_t &forfeitblocks)
{
    char CCaddr[64]; uint64_t sbits; uint256 txid,hashBlock; CTransaction tx;
    std::vector<std::pair<CAddressIndexKey, CAmount> > txids;
    GetCCaddress(cp,CCaddr,dicepk);
    SetCCtxids(txids,CCaddr);
    for (std::vector<std::pair<CAddressIndexKey, CAmount> >::const_iterator it=txids.begin(); it!=txids.end(); it++)
    {
        //int height = it->first.blockHeight;
        txid = it->first.txhash;
        if ( GetTransaction(txid,tx,hashBlock,false) != 0 && tx.vout.size() > 0 && ConstrainVout(tx.vout[0],1,CCaddr,0) != 0 )
        {
            if ( DecodeDiceFundingOpRet(tx.vout[tx.vout.size()-1].scriptPubKey,sbits,minbet,maxbet,maxodds,forfeitblocks) == 'F' )
            {
                if ( sbits == refsbits )
                    return(true);
            }
        }
    }
    return(false);
}

UniValue DiceInfo(uint256 diceid)
{
    UniValue result(UniValue::VOBJ); uint256 hashBlock; CTransaction vintx; int64_t minbet,maxbet,maxodds,forfeitblocks; uint64_t sbits; char str[67],numstr[65];
    if ( GetTransaction(diceid,vintx,hashBlock,false) == 0 )
    {
        fprintf(stderr,"cant find fundingtxid\n");
        result.push_back(Pair("error","cant find fundingtxid"));
        return(result);
    }
    if ( vintx.vout.size() > 0 && DecodeDiceFundingOpRet(vintx.vout[vintx.vout.size()-1].scriptPubKey,sbits,minbet,maxbet,maxodds,forfeitblocks) == 0 )
    {
        fprintf(stderr,"fundingtxid isnt dice creation txid\n");
        result.push_back(Pair("error","fundingtxid isnt dice creation txid"));
        return(result);
    }
    result.push_back(Pair("result","success"));
    result.push_back(Pair("fundingtxid",uint256_str(str,diceid)));
    unstringbits(str,sbits);
    result.push_back(Pair("name",str));
    result.push_back(Pair("sbits",sbits));
    sprintf(numstr,"%.8f",(double)minbet/COIN);
    result.push_back(Pair("minbet",numstr));
    sprintf(numstr,"%.8f",(double)maxbet/COIN);
    result.push_back(Pair("maxbet",numstr));
    result.push_back(Pair("maxodds",maxodds));
    result.push_back(Pair("forfeitblocks",forfeitblocks));
    sprintf(numstr,"%.8f",(double)vintx.vout[0].nValue/COIN);
    result.push_back(Pair("funding",numstr));
    return(result);
}

UniValue DiceList()
{
    UniValue result(UniValue::VARR); std::vector<std::pair<CAddressIndexKey, CAmount> > addressIndex; struct CCcontract_info *cp,C; uint256 txid,hashBlock; CTransaction vintx; uint64_t sbits; int64_t minbet,maxbet,maxodds,forfeitblocks; char str[65];
    cp = CCinit(&C,EVAL_DICE);
    SetCCtxids(addressIndex,cp->normaladdr);
    for (std::vector<std::pair<CAddressIndexKey, CAmount> >::const_iterator it=addressIndex.begin(); it!=addressIndex.end(); it++)
    {
        txid = it->first.txhash;
        if ( GetTransaction(txid,vintx,hashBlock,false) != 0 )
        {
            if ( vintx.vout.size() > 0 && DecodeDiceFundingOpRet(vintx.vout[vintx.vout.size()-1].scriptPubKey,sbits,minbet,maxbet,maxodds,forfeitblocks) != 0 )
            {
                result.push_back(uint256_str(str,txid));
            }
        }
    }
    return(result);
}

struct CCcontract_info *Diceinit(struct CCcontract_info *C,char *planstr,uint64_t &txfee,CPubKey &mypk,CPubKey &dicepk,uint64_t &sbits)
{
    int64_t a,b,c,d; struct CCcontract_info *cp;
    cp = CCinit(C,EVAL_DICE);
    if ( txfee == 0 )
        txfee = 10000;
    mypk = pubkey2pk(Mypubkey());
    dicepk = GetUnspendable(cp,0);
    sbits = stringbits(planstr);
    if ( DicePlanExists(cp,sbits,dicepk,a,b,c,d) != 0 )
    {
        fprintf(stderr,"Dice plan (%s) already exists\n",planstr);
        return(0);
    }
    return(cp);
}

std::string DiceCreateFunding(uint64_t txfee,char *planstr,int64_t funds,int64_t minbet,int64_t maxbet,int64_t maxodds,int64_t forfeitblocks)
{
    CMutableTransaction mtx; CPubKey mypk,dicepk; CScript opret; uint64_t sbits; struct CCcontract_info *cp,C;
    if ( funds < 0 || minbet < 0 || maxbet < 0 || maxodds < 1 || forfeitblocks < 0 || forfeitblocks > 1440 )
    {
        fprintf(stderr,"negative parameter error\n");
        return(0);
    }
    if ( (cp= Diceinit(&C,planstr,txfee,mypk,dicepk,sbits)) == 0 )
        return(0);
    if ( AddNormalinputs(mtx,mypk,funds+2*txfee,64) > 0 )
    {
        mtx.vout.push_back(MakeCC1vout(cp->evalcode,funds,dicepk));
        mtx.vout.push_back(CTxOut(txfee,CScript() << ParseHex(HexStr(dicepk)) << OP_CHECKSIG));
        return(FinalizeCCTx(0,cp,mtx,mypk,txfee,EncodeDiceFundingOpRet('F',sbits,minbet,maxbet,maxodds,forfeitblocks)));
    }
    fprintf(stderr,"cant find enough inputs\n");
    return(0);
}

std::string DiceAddfunding(uint64_t txfee,char *planstr,uint256 fundingtxid,int64_t amount)
{
    CMutableTransaction mtx; uint256 entropy,hentropy; CPubKey mypk,dicepk; CScript opret; uint64_t sbits; struct CCcontract_info *cp,C;
    if ( amount < 0 )
    {
        fprintf(stderr,"negative parameter error\n");
        return(0);
    }
    if ( (cp= Diceinit(&C,planstr,txfee,mypk,dicepk,sbits)) == 0 )
        return(0);
    if ( AddNormalinputs(mtx,mypk,amount+2*txfee,64) > 0 )
    {
        hentropy = DiceHashEntropy(entropy,mtx.vin[0].prevout.hash);
        mtx.vout.push_back(MakeCC1vout(cp->evalcode,amount,dicepk));
        mtx.vout.push_back(CTxOut(txfee,CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG));
        return(FinalizeCCTx(0,cp,mtx,mypk,txfee,EncodeDiceOpRet('E',sbits,fundingtxid,hentropy)));
    } else fprintf(stderr,"cant find enough inputs\n");
    fprintf(stderr,"cant find fundingtxid\n");
    return(0);
}

std::string DiceWinner(uint64_t txfee,char *planstr,uint256 fundingtxid,uint256 bettxid)
{
    CMutableTransaction mtx; uint256 entropy,hentropy; CPubKey mypk,dicepk; CScript opret; uint64_t sbits; struct CCcontract_info *cp,C; uint64_t amount = 0;
    if ( amount < 0 )
    {
        fprintf(stderr,"negative parameter error\n");
        return(0);
    }
    if ( (cp= Diceinit(&C,planstr,txfee,mypk,dicepk,sbits)) == 0 )
        return(0);
    if ( AddNormalinputs(mtx,mypk,amount+2*txfee,64) > 0 )
    {
        hentropy = DiceHashEntropy(entropy,mtx.vin[0].prevout.hash);
        mtx.vout.push_back(MakeCC1vout(cp->evalcode,amount,dicepk));
        mtx.vout.push_back(CTxOut(txfee,CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG));
        return(FinalizeCCTx(0,cp,mtx,mypk,txfee,EncodeDiceOpRet('W',sbits,fundingtxid,hentropy)));
    } else fprintf(stderr,"cant find enough inputs\n");
    fprintf(stderr,"cant find fundingtxid\n");
    return(0);
}

std::string DiceLoser(uint64_t txfee,char *planstr,uint256 fundingtxid,uint256 bettxid)
{
    CMutableTransaction mtx; uint256 entropy,hentropy; CPubKey mypk,dicepk; CScript opret; uint64_t sbits; struct CCcontract_info *cp,C; uint64_t amount = 0;
    if ( amount < 0 )
    {
        fprintf(stderr,"negative parameter error\n");
        return(0);
    }
    if ( (cp= Diceinit(&C,planstr,txfee,mypk,dicepk,sbits)) == 0 )
        return(0);
    if ( AddNormalinputs(mtx,mypk,amount+2*txfee,64) > 0 )
    {
        hentropy = DiceHashEntropy(entropy,mtx.vin[0].prevout.hash);
        mtx.vout.push_back(MakeCC1vout(cp->evalcode,amount,dicepk));
        mtx.vout.push_back(CTxOut(txfee,CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG));
        return(FinalizeCCTx(0,cp,mtx,mypk,txfee,EncodeDiceOpRet('W',sbits,fundingtxid,hentropy)));
    } else fprintf(stderr,"cant find enough inputs\n");
    fprintf(stderr,"cant find fundingtxid\n");
    return(0);
}

std::string DiceBet(uint64_t txfee,char *planstr,uint256 fundingtxid,int64_t bet,int32_t odds)
{
    CMutableTransaction mtx; CPubKey mypk,dicepk; CScript opret; uint64_t sbits,entropyval; int64_t funding,minbet,maxbet,maxodds,forfeitblocks; uint256 entropytxid,entropy,hentropy; struct CCcontract_info *cp,C;
    if ( bet < 0 || odds < 1 )
    {
        fprintf(stderr,"negative parameter error\n");
        return(0);
    }
    if ( (cp= Diceinit(&C,planstr,txfee,mypk,dicepk,sbits)) == 0 )
        return(0);
    if ( DicePlanExists(cp,sbits,dicepk,minbet,maxbet,maxodds,forfeitblocks) == 0 )
    {
        fprintf(stderr,"Dice plan %s doesnt exist\n",planstr);
        return(0);
    }
    if ( bet < minbet || bet > maxbet || odds > maxodds )
    {
        fprintf(stderr,"Dice plan %s illegal bet %.8f: minbet %.8f maxbet %.8f or odds %d vs max.%d\n",planstr,(double)bet/COIN,(double)minbet/COIN,(double)maxbet/COIN,(int32_t)odds,(int32_t)maxodds);
        return(0);
    }
    if ( (funding= DicePlanFunds(entropyval,entropytxid,sbits,cp,dicepk,fundingtxid)) >= bet*odds+txfee && entropyval != 0 )
    {
        mtx.vin.push_back(CTxIn(entropytxid,0,CScript()));
        if ( AddNormalinputs(mtx,mypk,bet+2*txfee+odds,60) > 0 )
        {
            hentropy = DiceHashEntropy(entropy,mtx.vin[0].prevout.hash);
            mtx.vout.push_back(MakeCC1vout(cp->evalcode,entropyval,dicepk));
            mtx.vout.push_back(MakeCC1vout(cp->evalcode,bet,dicepk));
            mtx.vout.push_back(CTxOut(txfee+odds,CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG));
            return(FinalizeCCTx(0,cp,mtx,mypk,txfee,EncodeDiceOpRet('B',sbits,fundingtxid,entropy)));
        } else fprintf(stderr,"cant find enough inputs %.8f note enough for %.8f\n",(double)funding/COIN,(double)bet/COIN);
    }
    if ( entropyval == 0 && funding != 0 )
        fprintf(stderr,"cant find dice entropy inputs\n");
    else fprintf(stderr,"cant find dice inputs\n");
    return(0);
}

std::string DiceUnlock(uint64_t txfee,char *planstr,uint256 fundingtxid,uint256 locktxid)
{
    int32_t houseflag = 1;
    CMutableTransaction mtx; CTransaction tx; char coinaddr[64]; CPubKey mypk,dicepk; CScript opret,scriptPubKey,ignore; uint256 hashBlock,entropy,hentropy; uint64_t funding,sbits,reward=0,amount=0,inputs,CCchange=0; int64_t minbet,maxbet,maxodds,forfeitblocks; struct CCcontract_info *cp,C;
    if ( (cp= Diceinit(&C,planstr,txfee,mypk,dicepk,sbits)) == 0 )
        return(0);
    if ( DicePlanExists(cp,sbits,dicepk,minbet,maxbet,maxodds,forfeitblocks) == 0 )
    {
        fprintf(stderr,"Dice plan %s doesnt exist\n",planstr);
        return(0);
    }
    // need to deal with finding the right utxos
    if ( locktxid == zeroid )
        amount = AddDiceInputs(scriptPubKey,0,cp,mtx,dicepk,(1LL << 30),1);
    else
    {
        GetCCaddress(cp,coinaddr,dicepk);
        if ( (amount= CCutxovalue(coinaddr,locktxid,0)) == 0 )
        {
            fprintf(stderr,"%s locktxid/v0 is spent\n",coinaddr);
            return(0);
        }
        if ( GetTransaction(locktxid,tx,hashBlock,false) != 0 && tx.vout.size() > 0 && tx.vout[1].scriptPubKey.IsPayToCryptoCondition() == 0 )
        {
            scriptPubKey = tx.vout[1].scriptPubKey;
            mtx.vin.push_back(CTxIn(locktxid,0,CScript()));
        }
        else
        {
            fprintf(stderr,"%s no normal vout.1 in locktxid\n",coinaddr);
            return(0);
        }
    }
    reward = 0;//DiceCalc(amount,mtx.vin[0].prevout.hash,minbet,maxbet,maxodds,forfeitblocks);
    if ( amount > 0 && reward > txfee && scriptPubKey.size() > 0 )
    {
        if ( (inputs= AddDiceInputs(ignore,1,cp,mtx,dicepk,reward+txfee,30)) > 0 )
        {
            if ( inputs >= (reward + 2*txfee) )
                CCchange = (inputs - (reward + txfee));
            fprintf(stderr,"inputs %.8f CCchange %.8f amount %.8f reward %.8f\n",(double)inputs/COIN,(double)CCchange/COIN,(double)amount/COIN,(double)reward/COIN);
            if ( houseflag != 0 )
                hentropy = DiceHashEntropy(entropy,mtx.vin[0].prevout.hash);
            else hentropy = zeroid;
            mtx.vout.push_back(MakeCC1vout(cp->evalcode,CCchange,dicepk));
            mtx.vout.push_back(CTxOut(amount+reward,scriptPubKey));
            return(FinalizeCCTx(-1LL,cp,mtx,mypk,txfee,EncodeDiceOpRet('U',sbits,fundingtxid,hentropy)));
        }
        fprintf(stderr,"cant find enough dice inputs\n");
    }
    fprintf(stderr,"amount %.8f -> reward %.8f\n",(double)amount/COIN,(double)reward/COIN);
    return(0);
}

