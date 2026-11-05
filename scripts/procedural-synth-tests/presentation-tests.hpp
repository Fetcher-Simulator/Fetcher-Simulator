void presentationTests()
{
    auto studio=std::make_unique<NativeStudio>();setupStudio(*studio);
    studio->set(1,"stringLayout",1);studio->set(1,"tune1",-2);
    const auto expression=[](unsigned frame,int string,int direction) {
        return scoreNote(frame,3+5u*unsigned(string+1)+45u*unsigned(direction+1),1,64,130);
    };
    const auto score=expression(0,1,1)+scoreNote(0,2400,1,64,100)
        +expression(0,6,0)+scoreNote(0,9600,1,64,100);
    check(studio->loadScore(score,12000,false) && studio->start(0,0),"presentation score starts");
    studioRender(*studio,1000);auto rows=studio->performance();
    check(rows.size()==2 && rows[0].selection==1 && rows[1].selection==6,"read-only snapshot preserves same-pitch string identities");
    check(rows[0].held && rows[1].held && rows[0].layout==1 && rows[0].transpose==-2
        && rows[0].articulation==2 && rows[0].direction==1 && rows[1].direction==0,"snapshot retains attack-time tuning and explicit articulation/strokes");
    const auto stamp=rows[0].stamp;
    studioRender(*studio,3000);rows=studio->performance();
    check(rows.size()==2 && !rows[0].held && rows[1].held && rows[0].stamp==stamp && rows[0].age>.08,
        "short released attack remains briefly visible without holding the other string's gate");
    studioRender(*studio,6000);rows=studio->performance();
    check(rows.size()==2 && !rows[1].held,"final gate releases while short attack history remains available");
    studio->stop();studioRender(*studio,480);check(studio->performance().empty(),"Stop clears all animation ownership/history");
    check(studio->strike(1,40,.8f,{-1,1,1}),"live expressive attack queues");studioRender(*studio,480);
    rows=studio->performance();check(rows.size()==1 && rows[0].held && rows[0].note==40 && rows[0].direction==1,"live and score use the same snapshot");
    const auto liveStamp=rows[0].stamp;
    studio->strike(1,40,.8f,{-1,1,0});studioRender(*studio,480);
    rows=studio->performance();check(rows[0].stamp!=liveStamp && rows[0].direction==0,"same-pitch retrigger has a fresh attack stamp");
    studio->noteOff(1,40);studioRender(*studio,480);check(!studio->performance()[0].held,"live key-up clears presentation hold");
    studio->releaseTrack(1);studioRender(*studio,480);check(studio->performance().empty(),"track panic clears recent attacks too");
    auto a=std::make_unique<NativeStudio>(),b=std::make_unique<NativeStudio>();setupStudio(*a);setupStudio(*b);
    a->loadScore(score,12000,true);b->loadScore(score,12000,true);a->start(0,0);b->start(0,0);
    std::vector<float> first,second;
    for(unsigned i=0;i<50;++i) {
        auto block=studioRender(*a,480);first.insert(first.end(),block.begin(),block.end());
        a->performance();block=studioRender(*b,480);second.insert(second.end(),block.begin(),block.end());
    }
    check(first==second,"presentation reads are bit-exact and do not consume or alter playback");
    a->close();check(a->performance().empty(),"closed stream never leaves a held visual note");
    auto touch=std::make_unique<NativeStudio>();setupStudio(*touch);
    touch->set(1,"instrumentType",1);touch->set(1,"stringLegato",1);touch->set(1,"stringLayout",1);touch->set(1,"stringTouch",.4f);
    touch->strike(1,40,.8f,{-1,1,0});studioRender(*touch,9600);
    touch->strike(1,42,.7f,{-1,1,0});studioRender(*touch,480);
    rows=touch->performance();bool unpicked=false;
    for(const auto& row:rows)if(row.note==42)unpicked=!row.picked;
    check(unpicked,"unpicked native connection is published without inventing a picked attack");
    std::cout<<"PASS native read-only performance snapshots, exact string identity, source timing, live retriggers and cleanup\n";
}
