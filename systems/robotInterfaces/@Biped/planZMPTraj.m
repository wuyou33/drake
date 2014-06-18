function [zmptraj, foottraj, support_times, supports] = planZMPTraj(biped, q0, footsteps, options)

if nargin < 4; options = struct(); end

if ~isfield(footsteps(1), 'zmp');
  zmp_pts = [];
else
  zmp_pts = [footsteps(2:end).zmp];
end
if ~isfield(options, 't0'); options.t0 = 0; end
if ~isfield(options, 'first_step_hold_s'); options.first_step_hold_s = 1; end

typecheck(biped,{'RigidBodyManipulator','TimeSteppingRigidBodyManipulator'});
typecheck(q0,'numeric');
sizecheck(q0,[biped.getNumDOF,1]);

is_right_foot = footsteps(1).frame_id == biped.foot_frame_id.right;

foot0 = feetPosition(biped, q0);
foot0.right(6) = foot0.left(6) + angleDiff(foot0.left(6), foot0.right(6));

steps.right = footsteps([footsteps.frame_id] == biped.foot_frame_id.right);
steps.left = footsteps([footsteps.frame_id] == biped.foot_frame_id.left);
steps.right(1).pos = foot0.right;
steps.left(1).pos = foot0.left;

for f = {'left', 'right'}
  foot = f{1};
  for k = 1:(length(steps.(foot))-1)
    p1 = steps.(foot)(k).pos;
    p2 = steps.(foot)(k+1).pos;
    steps.(foot)(k+1).pos = [p2(1:3); p1(4:6) + angleDiff(p1(4:6), p2(4:6))];
  end
end

function pos = feetCenter(rstep,lstep)
  rcen = rstep.pos;
  lcen = lstep.pos;
  pos = mean([rcen(1:3,:),lcen(1:3,:)],2);
end

supp0 = struct('right', steps.right(1).is_in_contact, 'left', steps.left(1).is_in_contact);
if isempty(zmp_pts)
  zmp0 = [];
  if supp0.right
    z = steps.right(1).pos;
    zmp0(:,end+1) = z(1:2);
  end
  if supp0.left
    z = steps.left(1).pos;
    zmp0(:,end+1) = z(1:2);
  end
  zmp0 = mean(zmp0, 2);
else
  zmp0 = zmp_pts(:,1);
end


step_knots = struct('t', options.t0, ...
  'right', steps.right(1).pos,...
  'left', steps.left(1).pos);
zmp_knots = struct('t', options.t0, 'zmp', zmp0, 'supp', supp0);

istep = struct('right', 1, 'left', 1);
is_first_step = true;

while 1
  if is_right_foot
    sw_foot = 'right'; % moving (swing) foot
    st_foot = 'left'; % stance foot
  else
    sw_foot = 'left';
    st_foot = 'right';
  end
  sw0 = steps.(sw_foot)(istep.(sw_foot));
  sw1 = steps.(sw_foot)(istep.(sw_foot)+1);
  st = steps.(st_foot)(istep.(st_foot));

  [swing_ts, swing_poses, takeoff_time, landing_time] = planSwing(biped, sw0, sw1);
  step_duration = (swing_ts(end) - swing_ts(1));
  if is_first_step
    swing_ts = swing_ts + options.first_step_hold_s;
    takeoff_time = takeoff_time + options.first_step_hold_s;
    landing_time = landing_time + options.first_step_hold_s;
    step_duration = step_duration + options.first_step_hold_s;
    is_first_step = false;
  end

  t0 = step_knots(end).t;
  for j = 1:length(swing_ts)
    step_knots(end+1).t = swing_ts(j) + t0;
    p = swing_poses.center(:,j);
    step_knots(end).(sw_foot) = p;
    step_knots(end).(st_foot) = st.pos;

    if ~sw1.walking_params.constrain_full_foot_pose && j >= 3 && j <= (length(swing_ts) - 4)
      % Release orientation constraints on the foot during the middle of the swing
      step_knots(end).(sw_foot)(4:5) = nan;
    end
  end

  if isempty(zmp_pts)
    instep_shift = [0.0;0.025;0];
    zmp1 = shift_step_inward(biped, st, instep_shift);
    zmp2 = feetCenter(sw1, st);
    zmp2 = zmp2(1:2);
  else
    zmp1 = zmp_pts(:,istep.right+istep.left-1);
    zmp2 = zmp_pts(:,istep.right+istep.left);
  end

  supp1 = struct('right', ~is_right_foot, 'left', is_right_foot);
  supp2 = struct('right', 1, 'left', 1);
  zmp_knots(end+1) = struct('t', t0 + 0.5 * takeoff_time, 'zmp', zmp1, 'supp', supp2);
  zmp_knots(end+1) = struct('t', t0 + takeoff_time, 'zmp', zmp1, 'supp', supp1);
  zmp_knots(end+1) = struct('t', t0 + mean([takeoff_time, landing_time]), 'zmp', zmp1, 'supp', supp1);
  zmp_knots(end+1) = struct('t', t0 + landing_time, 'zmp', zmp1, 'supp', supp2);
  zmp_knots(end+1) = struct('t', t0 + step_duration, 'zmp', zmp2, 'supp', supp2);

  istep.(sw_foot) = istep.(sw_foot) + 1;

  is_right_foot = ~is_right_foot;
  if istep.left == length(steps.left) && istep.right == length(steps.right)
    break
  end
end

% add a segment at the end to recover
t0 = step_knots(end).t;
step_knots(end+1) = step_knots(end);
step_knots(end).t = t0 + 1.5;
zmpf = feetCenter(steps.right(end), steps.left(end));
zmpf = zmpf(1:2);
zmp_knots(end+1) =  struct('t', step_knots(end).t, 'zmp', zmpf, 'supp', struct('right', 1, 'left', 1));

% Build trajectories
for f = {'right', 'left'}
  foot = f{1};
  foottraj.(foot) = PPTrajectory(foh([step_knots.t], [step_knots.(foot)]));
end
zmptraj = PPTrajectory(foh([zmp_knots.t], [zmp_knots.zmp]));

% create support body trajectory
rfoot_body_idx = biped.getFrame(biped.foot_frame_id.right).body_ind;
lfoot_body_idx = biped.getFrame(biped.foot_frame_id.left).body_ind;
support_times = [zmp_knots.t];
supps = [zmp_knots.supp];
foot_supports = [[supps.right] * rfoot_body_idx;
                 [supps.left] * lfoot_body_idx];
zmp_ts = [zmp_knots.t];
supports = SupportState.empty(0,1);
for i=1:length(zmp_ts)
  if all(foot_supports(:,i)~=0)
    cpts = {1:4,1:4};
  else
    cpts = {1:4};
  end
  supports(i) = SupportState(biped,foot_supports(:,i),cpts);
end

end

function pos = shift_step_inward(biped, step, instep_shift)
  if step.frame_id == biped.foot_frame_id.left
    instep_shift = [1;-1;1].*instep_shift;
  end
  pos_center = step.pos;
  R = rpy2rotmat(pos_center(4:6));
  shift = R*instep_shift;
  pos = pos_center(1:2) + shift(1:2);
end
