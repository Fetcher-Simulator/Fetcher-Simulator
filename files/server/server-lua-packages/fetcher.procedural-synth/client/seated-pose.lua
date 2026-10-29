-- Expiring presentation only: lower pelvis, bend knees and retain planted feet.
return function(anim,util,solve,between)
    local S={};local v,T=util.vector3,util.transform;local zero=v(0,0,0)
    function S.supported(bones)
        if not bones['bip01 pelvis'] then return false end
        for _,side in ipairs{'l','r'} do for _,part in ipairs{'thigh','calf','foot'} do
            if not bones['bip01 '..side..' '..part] then return false end
        end end
        return true
    end
    function S.apply(p,root,profile,setBone)
        if not S.supported(p.bones) then return zero,false end
        local pelvis=p.bones['bip01 pelvis'];local world=anim.getBoneTransform(p.actor,pelvis,true,true)
        local localPose=anim.getBoneTransform(p.actor,pelvis,false,true)
        if not world or not localPose then return zero,false end
        local seat=profile.seat;local shift=p.actor.rotation*seat.pelvis;local actual=shift*p.amount
        local parent=world*localPose:inverse();local inv=parent:inverse();local at=world*zero
        setBone(pelvis,T.identity,inv*(at+shift)-inv*at)
        p.appliedBones[pelvis]=true
        local toModel=root:inverse()
        for _,side in ipairs{'L','R'} do
            local thigh=p.bones[('bip01 '..side..' thigh'):lower()];local calf=p.bones[('bip01 '..side..' calf'):lower()];local foot=p.bones[('bip01 '..side..' foot'):lower()]
            local u=anim.getBoneTransform(p.actor,thigh,true,true);local f=anim.getBoneTransform(p.actor,calf,true,true);local h=anim.getBoneTransform(p.actor,foot,true,true)
            if u and f and h then
                local footAt=h*zero;u,f,h=toModel*T.move(actual)*u,toModel*T.move(actual)*f,toModel*T.move(actual)*h
                local a,b,c=u*zero,f*zero,h*zero
                local target=toModel*(footAt+p.actor.rotation*seat.targets[side])
                local pole=toModel*(root*a+p.actor.rotation*seat.poles[side])-a
                local knee,tip=solve(a,target,(b-a):length(),(c-b):length(),pole)
                if knee then
                    local inverse=u:inverse();local upper=between(inverse*b,inverse*knee);local adjusted=u*upper*inverse
                    local parentFore=adjusted*f;inverse=parentFore:inverse();local lower=between(inverse*(adjusted*c),inverse*tip)
                    local posed=parentFore*lower*parentFore:inverse()*adjusted*h
                    local desired=T.move(tip-h*zero)*h*seat.rotations[side]
                    p.legReachError=math.max(p.legReachError,(root*target-root*tip):length())
                    -- Bone rotations require a quaternion, not a matrix transform.
                    local z,y,x=(posed:inverse()*desired):getAnglesZYX()
                    local footRotation=T.rotateZ(z)*T.rotateY(y)*T.rotateX(x)
                    for name,rotation in pairs{[thigh]=upper,[calf]=lower,[foot]=footRotation} do
                        setBone(name,rotation);p.appliedBones[name]=true
                    end
                end
            end
        end
        return actual,true
    end
    return S
end
