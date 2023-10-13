#include "AuctionatorBidder.h"
#include "Auctionator.h"
#include "ObjectMgr.h"


AuctionatorBidder::AuctionatorBidder(Auctionator* natorParam, uint32 auctionHouseIdParam)
{
    nator = natorParam;
    auctionHouseId = auctionHouseIdParam;
    ahMgr = nator->GetAuctionMgr(auctionHouseId);
    buyerGuid = ObjectGuid::Create<HighGuid::Player>(gAuctionator->config->characterGuid);
}

AuctionatorBidder::~AuctionatorBidder()
{

}

void AuctionatorBidder::SpendSomeCash()
{
    uint32 auctionatorPlayerGuid = nator->config->characterGuid;

    std::string query = "SELECT id FROM auctionhouse WHERE itemowner <> {}; ";

    QueryResult result = CharacterDatabase.Query(query, auctionatorPlayerGuid);

    if (!result) {
        gAuctionator->logInfo("Can't see player auctions, moving on.");
        return;
    }

    if (result->GetRowCount() == 0) {
        gAuctionator->logInfo("No player auctions, taking my money elsewhere.");
        return;
    }

    std::vector<uint32> biddableAuctionIds;
    do {
        biddableAuctionIds.push_back(result->Fetch()->Get<uint32>());
    } while(result->NextRow());

    nator->logInfo("Found " + std::to_string(biddableAuctionIds.size()) + " biddable auctions");

    uint32 purchasePerCycle = 5;
    uint32 counter = 0;
    uint32 total = biddableAuctionIds.size();

    while(purchasePerCycle > 0 && biddableAuctionIds.size() > 0) {
        counter++;
        AuctionEntry* auction = GetAuctionForPurchase(biddableAuctionIds);

        if (auction == nullptr) {
            return;
        }

        Item *item = sAuctionMgr->GetAItem(auction->item_guid);
        ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(auction->item_template);

        nator->logInfo("Considering auction: "
            + itemTemplate->Name1
            + "(" + std::to_string(auction->Id) + ")"
            + ", " + std::to_string(counter) + " of "
            + std::to_string(total)
        );

        bool success = false;

        if (auction->buyout > 0) {
            success = BuyoutAuction(auction, item, itemTemplate);
        } else {
            success = BidOnAuction(auction, item, itemTemplate);
        }



        // if (success) {
            purchasePerCycle--;
        // }
    }

}

AuctionEntry* AuctionatorBidder::GetAuctionForPurchase(std::vector<uint32>& auctionIds)
{
    if (auctionIds.size() == 0) {
        return nullptr;
    }

    uint32 auctionId = auctionIds[0];
    auctionIds.erase(auctionIds.begin());

    gAuctionator->logTrace("Auction removed, remaining items: " + std::to_string(auctionIds.size()));

    AuctionEntry* auction = ahMgr->GetAuction(auctionId);
    return auction;
}

bool AuctionatorBidder::BidOnAuction(AuctionEntry* auction, Item* item, ItemTemplate const* itemTemplate)
{
    uint32 currentPrice;

    if (auction->bid) {
        if (auction->bidder == buyerGuid) {
            gAuctionator->logInfo("Skipping auction, I have already bid: "
                + std::to_string(auction->bid) + ".");
        } else {
            gAuctionator->logInfo("Skipping auction, someone else has already bid "
                + std::to_string(auction->bid) + ".");
        }
        return false;
    } else {
        currentPrice = auction->startbid;
    }

    if (currentPrice > itemTemplate->BuyPrice) {
        gAuctionator->logInfo("Skipping auction ("
            + std::to_string(auction->Id) + "), price of "
            + std::to_string(currentPrice) + " is higher than template price of "
            + std::to_string(itemTemplate->BuyPrice)
        );
        return false;
    }

    uint32 bidPrice = currentPrice + (itemTemplate->BuyPrice - currentPrice) / 2;

    auction->bidder = buyerGuid;
    auction->bid = bidPrice;

    CharacterDatabase.Execute(R"(
            UPDATE
                auctionhouse
            SET
                buyguid = {},
                lastbid = {}.
            WHERE
                id = {}
        )",
        auction->bidder.GetCounter(),
        auction->bid,
        auction->Id
    );

    gAuctionator->logInfo("Bid on auction of "
        + itemTemplate->Name1 + " ("
        + std::to_string(auction->Id) + ") of "
        + std::to_string(bidPrice) + " copper."
    );

    return true;
}

bool AuctionatorBidder::BuyoutAuction(AuctionEntry* auction, Item* item, ItemTemplate const* itemTemplate)
{
    if (auction->buyout > itemTemplate->BuyPrice) {
        gAuctionator->logInfo("Skipping buyout, price is higher than template buyprice");
        return false;
    }

    auto trans = CharacterDatabase.BeginTransaction();
    auction->bidder = buyerGuid;
    auction->bid = auction->buyout;

    sAuctionMgr->SendAuctionSuccessfulMail(auction, trans);
    auction->DeleteFromDB(trans);

    sAuctionMgr->RemoveAItem(auction->item_guid);
    ahMgr->RemoveAuction(auction);

    CharacterDatabase.CommitTransaction(trans);

    gAuctionator->logInfo("Purchased auction of "
        + itemTemplate->Name1 + " ("
        + std::to_string(auction->Id) + ") for "
        + std::to_string(auction->buyout) + " copper."
    );

    return true;
}