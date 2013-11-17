#include "Constraint.h"
using namespace Eigen;


double angleDiff(double phi1, double phi2)
{
  double d = phi2-phi1;
  if(d>0.0)
  {
    d = fmod(d+M_PI,2*M_PI)-M_PI;
  }
  else
  {
    d = fmod(d-M_PI,2*M_PI)+M_PI;
  }
  return d;
}
template <typename T>
void myrealloc(T* &ptr, int old_size, int new_size)
{
  T* newptr = new T[new_size];
  if (old_size>0){
    int s = old_size;
    if (new_size<old_size) s = new_size;
    for (int i = 0;i<s;i++) newptr[i] = ptr[i];
    delete[] ptr;
  }
  ptr = newptr;
}

void drakeMexPrintMatrix(const MatrixXd &mat)
{
  for(int i = 0;i<mat.rows();i++)
  {
    for(int j = 0;j<mat.cols();j++)
    {
      mexPrintf("%7.3f ",mat(i,j));
    }
    mexPrintf("\n");
  }
};

mxArray* createDrakeConstraintMexPointer(void* ptr, const char* deleteMethod, const char* name)
{
	mxClassID cid;
	if (sizeof(ptr)==4) cid = mxUINT32_CLASS;
	else if (sizeof(ptr)==8) cid = mxUINT64_CLASS;
  else mexErrMsgIdAndTxt("Drake:createDrakeConstraintMexPointer:PointerSize","Are you on a 32-bit machine or 64-bit machine??");

	int nrhs=3;
	mxArray *prhs[nrhs], *plhs[1];

	prhs[0] = mxCreateNumericMatrix(1,1,cid,mxREAL);
  memcpy(mxGetData(prhs[0]),&ptr,sizeof(ptr));

	prhs[1] = mxCreateString(deleteMethod);

  prhs[2] = mxCreateString(name);

  // call matlab to construct mex pointer object
  mexCallMATLABsafe(1,plhs,nrhs,prhs,"DrakeConstraintMexPointer");

  return plhs[0];
}


const int QuasiStaticDefaultRobotNum[1] = {0};
const std::set<int> QuasiStaticConstraint::defaultRobotNumSet(QuasiStaticDefaultRobotNum,QuasiStaticDefaultRobotNum+1);
QuasiStaticConstraint::QuasiStaticConstraint(RigidBodyManipulator* robot, const Vector2d &tspan,const std::set<int> &robotnumset):Constraint(DrakeConstraintType::QuasiStaticConstraintType)
{
  this->robot = robot;
  this->m_robotnumset = robotnumset;
  assert(tspan(0)<=tspan(1));
  this->tspan[0] = tspan(0);
  this->tspan[1] = tspan(1);
  this->shrinkFactor = 0.9;
  this->active = false;
  this->num_bodies = 0;
  this->num_pts = 0;
}

QuasiStaticConstraint::~QuasiStaticConstraint()
{
}

bool QuasiStaticConstraint::isTimeValid(double* t)
{
  if(t == NULL) return true;
  return (*t)>=this->tspan[0]&&(*t)<=this->tspan[1];
}

int QuasiStaticConstraint::getNumConstraint(double* t)
{
  if(this->isTimeValid(t))
  {
    return 3;
  }
  else
  {
    return 0;
  }
}

void QuasiStaticConstraint::updateRobot(RigidBodyManipulator* robot)
{
  this->robot = robot;
}
void QuasiStaticConstraint::eval(double* t,double* weights, VectorXd &c, MatrixXd &dc)
{
  if(this->isTimeValid(t))
  {
    int nq = this->robot->num_dof;
    dc.resize(2,nq+this->num_pts);
    Vector3d com;
    this->robot->getCOM(com,this->m_robotnumset);
    MatrixXd dcom;
    this->robot->getCOMJac(dcom,this->m_robotnumset);
    MatrixXd contact_pos(3,this->num_pts);
    MatrixXd dcontact_pos(3*this->num_pts,nq);
    int num_accum_pts = 0;
    Vector3d center_pos= Vector3d::Zero();
    MatrixXd dcenter_pos = MatrixXd::Zero(3,nq);
    for(int i = 0;i<this->num_bodies;i++)
    {
      MatrixXd body_contact_pos(3,this->num_body_pts[i]);
      MatrixXd dbody_contact_pos(3*this->num_body_pts[i],nq);
      this->robot->forwardKin(this->bodies[i],this->body_pts[i],0,body_contact_pos);
      this->robot->forwardJac(this->bodies[i],this->body_pts[i],0,dbody_contact_pos);
      contact_pos.block(0,num_accum_pts,3,this->num_body_pts[i]) = body_contact_pos;
      dcontact_pos.block(3*num_accum_pts,0,3*this->num_body_pts[i],nq) = dbody_contact_pos;
      for(int j = 0;j<this->num_body_pts[i];j++)
      {
        center_pos = center_pos+body_contact_pos.col(j);
        dcenter_pos = dcenter_pos+dbody_contact_pos.block(3*j,0,3,nq);
      }
      num_accum_pts += this->num_body_pts[i];
    }
    center_pos = center_pos/this->num_pts;
    dcenter_pos = dcenter_pos/this->num_pts;
    MatrixXd support_pos(2,this->num_pts);
    MatrixXd dsupport_pos(2*this->num_pts,nq);
    c = com.head(2);
    dc.block(0,0,2,nq) = dcom.block(0,0,2,nq);
    for(int i = 0;i<this->num_pts;i++)
    {
      support_pos.col(i) = center_pos.head(2)*(1.0-this->shrinkFactor)+contact_pos.block(0,i,2,1)*this->shrinkFactor;
      dsupport_pos.block(2*i,0,2,nq) = dcenter_pos.block(0,0,2,nq)*(1.0-this->shrinkFactor)+dcontact_pos.block(3*i,0,2,nq)*this->shrinkFactor;
      c = c-weights[i]*support_pos.col(i);
      dc.block(0,0,2,nq) = dc.block(0,0,2,nq)-weights[i]*dsupport_pos.block(2*i,0,2,nq);
    }
    dc.block(0,nq,2,this->num_pts) = -support_pos;
  }
  else
  {
    c.resize(0);
    dc.resize(0,0);
  }
}

void QuasiStaticConstraint::bounds(double* t,VectorXd &lb, VectorXd &ub)
{
  if(this->isTimeValid(t))
  {
    lb.resize(2);
    ub.resize(2);
    lb<<0.0,0.0;
    ub<<0.0,0.0;
  }
  else
  {
    lb.resize(0);
    ub.resize(0);
  }
}

void QuasiStaticConstraint::name(double* t,std::vector<std::string> &name_str)
{
  if(t == NULL)
  {
    char cnst_name_str_buffer[500]; 
    sprintf(cnst_name_str_buffer,"QuasiStaticConstraint x");
    std::string cnst_name_str1(cnst_name_str_buffer);
    name_str.push_back(cnst_name_str1);
    sprintf(cnst_name_str_buffer,"QuasiStaticConstraint y");
    std::string cnst_name_str2(cnst_name_str_buffer);
    name_str.push_back(cnst_name_str2);
  }
  else
  {
    char cnst_name_str_buffer[500]; 
    sprintf(cnst_name_str_buffer,"QuasiStaticConstraint x at time %10.4f",*t);
    std::string cnst_name_str1(cnst_name_str_buffer);
    name_str.push_back(cnst_name_str1);
    sprintf(cnst_name_str_buffer,"QuasiStaticConstraint y at time %10.4f",*t);
    std::string cnst_name_str2(cnst_name_str_buffer);
    name_str.push_back(cnst_name_str2);
  }
}

bool compare3Dvector(const Vector3d& a, const Vector3d& b)
{
  if(a(0)<b(0)) return true;
  if(a(0)>b(0)) return false;
  if(a(1)<b(1)) return true;
  if(a(1)>b(1)) return false;
  if(a(2)<b(2)) return true;
  return false;
}
void QuasiStaticConstraint::addContact(int num_new_bodies,const int* new_bodies, const MatrixXd* new_body_pts)
{
  for(int i = 0;i<num_new_bodies;i++)
  {
    bool findDuplicateBody = false;
    assert(new_body_pts[i].rows() == 4);
    for(int j = 0;j<this->num_bodies;j++)
    {
      if(this->bodies[j] == new_bodies[i])
      {
        findDuplicateBody = true;
        bool (*compare3Dvector_ptr)(const Vector3d&,const Vector3d&) = compare3Dvector;
        std::set<Vector3d,bool(*)(const Vector3d&, const Vector3d&)> unique_body_pts (compare3Dvector_ptr);
        for(int k = 0;k<this->body_pts[j].cols();k++)
        {
          unique_body_pts.insert(body_pts[j].block(0,k,3,1));
        }
        for(int k = 0;k<new_body_pts[i].cols();k++)
        {
          unique_body_pts.insert(new_body_pts[i].block(0,k,3,1));
        }
        this->num_pts -= this->num_body_pts[j];
        this->num_body_pts[j] = unique_body_pts.size();
        this->num_pts += this->num_body_pts[j];
        this->body_pts[j].resize(4,this->num_body_pts[j]);
        int col_idx = 0;
        for(auto it = unique_body_pts.begin();it!=unique_body_pts.end();it++)
        {
          this->body_pts[j].block(0, col_idx,3,1) = *it;
          col_idx++;
        }
        this->body_pts[j].row(3) = MatrixXd::Ones(1,this->num_body_pts[j]);
      }
    }
    if(!findDuplicateBody)
    {
      bodies.push_back(new_bodies[i]);
      num_body_pts.push_back(new_body_pts[i].cols());
      body_pts.push_back(new_body_pts[i]);
      num_bodies++;
      num_pts += new_body_pts[i].cols();
    }
  }
}

void QuasiStaticConstraint::setShrinkFactor(double factor)
{
  assert(factor>0.0);
  this->shrinkFactor = factor;
}
bool PostureConstraint::isTimeValid(double* t)
{
  if(t == NULL) return true;
  return (*t)>=this->tspan[0]&&(*t)<=this->tspan[1];
}

void QuasiStaticConstraint::updateRobotnum(std::set<int> &robotnumset)
{
  this->m_robotnumset = robotnumset;
}

PostureConstraint::PostureConstraint(RigidBodyManipulator* model, const Eigen::Vector2d &tspan):Constraint(DrakeConstraintType::PostureConstraintType)
{
  assert(tspan(1)>=tspan(0));
  this->tspan[0] = tspan(0);
  this->tspan[1] = tspan(1);
  this->robot = model;
  int nq = this->robot->num_dof;
  joint_limit_min = new double[nq];
  joint_limit_max = new double[nq];
  memcpy(joint_limit_min,this->robot->joint_limit_min,sizeof(double)*nq);
  memcpy(joint_limit_max,this->robot->joint_limit_max,sizeof(double)*nq);
}


void PostureConstraint::setJointLimits(int num_idx,const int* joint_idx, const double* lb, const double* ub)
{
  for(int i = 0;i<num_idx;i++)
  {
    this->joint_limit_min[joint_idx[i]] = (this->robot->joint_limit_min[joint_idx[i]]<lb[i]? lb[i]:this->robot->joint_limit_min[joint_idx[i]]);
    this->joint_limit_max[joint_idx[i]] = (this->robot->joint_limit_max[joint_idx[i]]>ub[i]? ub[i]:this->robot->joint_limit_max[joint_idx[i]]);
  }
}

void PostureConstraint::bounds(double* t, double* joint_min, double* joint_max)
{
  if(this->isTimeValid(t))
  {
    int nq = this->robot->num_dof;
    memcpy(joint_min,this->joint_limit_min,sizeof(double)*nq);
    memcpy(joint_max,this->joint_limit_max,sizeof(double)*nq);
  }
  else
  {
    int nq = this->robot->num_dof;
    memcpy(joint_min,this->robot->joint_limit_min,sizeof(double)*nq);
    memcpy(joint_max,this->robot->joint_limit_max,sizeof(double)*nq);
  }
}

PostureConstraint::~PostureConstraint()
{
  delete[] joint_limit_min;
  delete[] joint_limit_max;
}


SingleTimeKinematicConstraint::SingleTimeKinematicConstraint(RigidBodyManipulator *model, const Vector2d &tspan): Constraint(DrakeConstraintType::SingleTimeKinematicConstraintType)
{
  assert(tspan(1)>=tspan(0));
  this->tspan[0] = tspan(0);
  this->tspan[1] = tspan(1);
  this->robot = model;
  this->num_constraint = 0;
}

bool SingleTimeKinematicConstraint::isTimeValid(double* t)
{
   if(t == NULL) return true;
   return (*t)>=this->tspan[0]&&(*t)<=this->tspan[1];
}

int SingleTimeKinematicConstraint::getNumConstraint(double* t)
{
  if(isTimeValid(t))
  {
    return this->num_constraint;
  }
  return 0;
}

MultipleTimeKinematicConstraint::MultipleTimeKinematicConstraint(RigidBodyManipulator *model, const Vector2d &tspan): Constraint(DrakeConstraintType::MultipleTimeKinematicConstraintType)
{
  assert(tspan(1)>=tspan(0));
  this->tspan[0] = tspan(0);
  this->tspan[1] = tspan(1);
  this->robot = model;
}

void MultipleTimeKinematicConstraint::eval(double* t, int n_breaks, const MatrixXd &q, VectorXd &c, MatrixXd &dc)
{
  int num_valid_t = this->numValidTime(t,n_breaks);
  if(num_valid_t>=2)
  {
    std::vector<bool> valid_time_flag = this->isTimeValid(t,n_breaks);
    int nq = this->robot->num_dof;
    double* valid_t = new double[num_valid_t];
    MatrixXd valid_q(nq,num_valid_t);
    int valid_idx = 0;
    int* valid2tMap = new int[num_valid_t];
    for(int i = 0;i<n_breaks;i++)
    {
      if(valid_time_flag[i])
      {
        valid_t[valid_idx] = t[i];
        valid_q.col(valid_idx) = q.col(i);
        valid2tMap[valid_idx] = i;
        valid_idx++;
      }
    }
    MatrixXd dc_valid;
    this->eval_valid(valid_t,num_valid_t,valid_q,c,dc_valid);
    int nc = this->getNumConstraint(t,n_breaks);
    dc = MatrixXd::Zero(nc,nq*n_breaks);
    for(int i = 0;i<num_valid_t;i++)
    {
      dc.block(0,valid2tMap[i]*nq,nc,nq) = dc_valid.block(0,i*nq,nc,nq);
    }
    delete[] valid_t;
    delete[] valid2tMap;
  }
  else
  {
    c.resize(0);
    dc.resize(0,0);
  }
}

std::vector<bool> MultipleTimeKinematicConstraint::isTimeValid(double* t,int n_breaks)
{
  std::vector<bool> flag;
  for(int i = 0;i<n_breaks;i++)
  {
    if((t[i]>this->tspan[1]||t[i]<this->tspan[0]))
    {
      flag.push_back(false);
    }
    else
    {
      flag.push_back(true);
    }
  }
  return flag;
}

int MultipleTimeKinematicConstraint::numValidTime(double* t, int n_breaks)
{
  std::vector<bool> valid_flag = this->isTimeValid(t,n_breaks);
  int num_valid_t = 0;
  for(auto it=valid_flag.begin();it != valid_flag.end(); it++)
  {
    if(*it)
    {
      num_valid_t++;
    }
  }
  return num_valid_t;
}

PositionConstraint::PositionConstraint(RigidBodyManipulator *model, const MatrixXd &pts, MatrixXd lb, MatrixXd ub, const Vector2d &tspan):SingleTimeKinematicConstraint(model,tspan)
{
  this->n_pts = pts.cols();
  assert(pts.rows() == 4);
  this->pts = pts;
  assert(lb.rows() == 3 &&lb.cols() == n_pts && ub.rows() == 3 && ub.cols() == n_pts);
  
  this->null_constraint_rows = new bool[3*n_pts];
  this->num_constraint = 0;
  for(int j = 0;j<n_pts;j++)
  {
    for(int i = 0;i<3;i++)
    {
      int idx = j*3+i;
      if(mxIsNaN(lb(i,j)))
      {
        lb(i,j) = -mxGetInf();
      }
      if(mxIsNaN(ub(i,j)))
      {
        ub(i,j) = mxGetInf();
      }
      if(ub(i,j)<lb(i,j))
      {
        mexErrMsgIdAndTxt("Drake:PositionConstraint:BadInputs","lb must be no larger than ub");
      }
      if(mxIsInf(lb(i,j))&&mxIsInf(ub(i,j)))
      {
        this->null_constraint_rows[idx] = true;
      }
      else
      {
        this->null_constraint_rows[idx] = false;
        this->num_constraint++;
      }
    }
  }
  this->lb = new double[this->num_constraint];
  this->ub = new double[this->num_constraint];
  int valid_row_idx = 0;
  int valid_col_idx = 0;
  int bnd_idx = 0;
  while(bnd_idx<this->num_constraint)
  {
    int idx = 3*valid_col_idx+valid_row_idx;
    if(!this->null_constraint_rows[idx])
    {
      this->lb[bnd_idx] = lb(valid_row_idx,valid_col_idx);
      this->ub[bnd_idx] = ub(valid_row_idx,valid_col_idx);
      bnd_idx++;
    }
    valid_row_idx++;
    if(valid_row_idx == 3)
    {
      valid_row_idx = 0;
      valid_col_idx++;
    }
  }
}

void PositionConstraint::eval(double* t, VectorXd &c, MatrixXd &dc)
{
  if(this->isTimeValid(t))
  {
    MatrixXd pos(3,this->n_pts);
    MatrixXd J(3*this->n_pts,this->robot->num_dof);
    this->evalPositions(pos,J);
    c.resize(this->getNumConstraint(t),1);
    dc.resize(this->getNumConstraint(t),this->robot->num_dof);
    int valid_row_idx = 0;
    int i = 0;
    while(i<this->getNumConstraint(t))
    {
      if(!this->null_constraint_rows[valid_row_idx])
      {
        c(i) = pos(valid_row_idx);
        dc.row(i) = J.row(valid_row_idx);
        i++;
        valid_row_idx++;
      }
      else
      {
        valid_row_idx++;
      }
    }
  }
  else
  {
    c.resize(0);
    dc.resize(0,0);
  }
}

void PositionConstraint::bounds(double* t,VectorXd &lb, VectorXd &ub)
{
  lb.resize(this->getNumConstraint(t));
  ub.resize(this->getNumConstraint(t));
  if(this->isTimeValid(t))
  {
    memcpy(lb.data(),this->lb,sizeof(double)*this->num_constraint);
    memcpy(ub.data(),this->ub,sizeof(double)*this->num_constraint);
  }
}

PositionConstraint::~PositionConstraint()
{
  delete[] this->lb;
  delete[] this->ub;
  delete[] this->null_constraint_rows;
}

WorldPositionConstraint::WorldPositionConstraint(RigidBodyManipulator *model, int body, const MatrixXd &pts, MatrixXd lb, MatrixXd ub, const Vector2d &tspan):PositionConstraint(model,pts,lb,ub,tspan)
{
  this->body = body;
  this->body_name = model->bodies[body].linkname;
}

void WorldPositionConstraint::evalPositions(MatrixXd &pos, MatrixXd &J)
{
  this->robot->forwardKin(this->body, this->pts,0,pos);
  this->robot->forwardJac(this->body, this->pts,0,J);
}

void WorldPositionConstraint::name(double* t, std::vector<std::string>& name_str)
{
  if(this->isTimeValid(t))
  {
    char cnst_name_str_buffer[500]; 
    int constraint_idx = 0;
    for(int i = 0;i<this->n_pts;i++)
    {
      if(!this->null_constraint_rows[3*i+0])
      {
        if(t == NULL)
        {
          sprintf(cnst_name_str_buffer,"%s pts(:,%d) x",this->body_name.c_str(),i+1);
        }
        else
        {
          sprintf(cnst_name_str_buffer,"%s pts(:,%d) x at time %10.4f",this->body_name.c_str(),i+1,*t);
        }
        std::string cnst_name_str(cnst_name_str_buffer);
        name_str.push_back(cnst_name_str);
        constraint_idx++;
      }
      if(!this->null_constraint_rows[3*i+1])
      {
        if(t == NULL)
        {
          sprintf(cnst_name_str_buffer,"%s pts(:,%d) y",this->body_name.c_str(),i+1);
        }
        else
        {
          sprintf(cnst_name_str_buffer,"%s pts(:,%d) y at time %10.4f",this->body_name.c_str(),i+1,*t);
        }
        std::string cnst_name_str(cnst_name_str_buffer);
        name_str.push_back(cnst_name_str);
        constraint_idx++;
      }
      if(!this->null_constraint_rows[3*i+2])
      {
        if(t == NULL)
        {
          sprintf(cnst_name_str_buffer,"%s pts(:,%d) z",this->body_name.c_str(),i+1);
        }
        else
        {
          sprintf(cnst_name_str_buffer,"%s pts(:,%d) z at time %10.4f",this->body_name.c_str(),i+1,*t);
        }
        std::string cnst_name_str(cnst_name_str_buffer);
        name_str.push_back(cnst_name_str);
        constraint_idx++;
      }
    }
  }
}

void WorldPositionConstraint::updateRobot(RigidBodyManipulator* robot)
{
  this->robot = robot;
}

WorldPositionConstraint::~WorldPositionConstraint()
{
}

Vector4d com_pts(0.0,0.0,0.0,1.0);


const int WorldCoMDefaultRobotNum[1] = {0};
const std::set<int> WorldCoMConstraint::defaultRobotNumSet(WorldCoMDefaultRobotNum,WorldCoMDefaultRobotNum+1);

WorldCoMConstraint::WorldCoMConstraint(RigidBodyManipulator *model, Vector3d lb, Vector3d ub, const Vector2d &tspan, const std::set<int> &robotnum):PositionConstraint(model,com_pts,lb,ub,tspan)
{
  this->m_robotnum = robotnum;
  this->body = -1;
  this->body_name = "CoM";
}

void WorldCoMConstraint::evalPositions(MatrixXd &pos, MatrixXd &J)
{
  this->robot->getCOM(pos,this->m_robotnum);
  this->robot->getCOMJac(J,this->m_robotnum);
}


void WorldCoMConstraint::name(double* t, std::vector<std::string> &name_str)
{
  if(this->isTimeValid(t))
  {
    char cnst_name_str_buffer[100]; 
    int constraint_idx = 0;
    if(!this->null_constraint_rows[0])
    {
      if(t == NULL)
      {
        sprintf(cnst_name_str_buffer,"CoM x");
      }
      else
      {
        sprintf(cnst_name_str_buffer,"CoM x at time %10.4f",*t);
      }
      std::string cnst_name_str(cnst_name_str_buffer);
      name_str.push_back(cnst_name_str);
      constraint_idx++;
    }
    if(!this->null_constraint_rows[1])
    {
      if(t == NULL)
      {
        sprintf(cnst_name_str_buffer,"CoM y");
      }
      else
      {
        sprintf(cnst_name_str_buffer,"CoM y at time %10.4f",*t);
      }
      std::string cnst_name_str(cnst_name_str_buffer);
      name_str.push_back(cnst_name_str);
      constraint_idx++;
    }
    if(!this->null_constraint_rows[2])
    {
      if(t == NULL)
      {
        sprintf(cnst_name_str_buffer,"CoM z");
      }
      else
      {
        sprintf(cnst_name_str_buffer,"CoM z at time %10.4f",*t);
      }
      std::string cnst_name_str(cnst_name_str_buffer);
      name_str.push_back(cnst_name_str);
      constraint_idx++;
    }
  }
}

void WorldCoMConstraint::updateRobot(RigidBodyManipulator* robot)
{
  this->robot = robot;
}

void WorldCoMConstraint::updateRobotnum(const std::set<int> &robotnum)
{
  this->m_robotnum = robotnum;
}

WorldCoMConstraint::~WorldCoMConstraint()
{
}

QuatConstraint::QuatConstraint(RigidBodyManipulator *model, double tol, Vector2d tspan):SingleTimeKinematicConstraint(model,tspan)
{
  assert(tol>=0.0&&tol<=1.0);
  this->tol = tol;
  this->num_constraint = 1;
}

void QuatConstraint::eval(double* t, VectorXd &c, MatrixXd &dc)
{
  int num_constraint = this->getNumConstraint(t);
  c.resize(num_constraint);
  dc.resize(num_constraint,this->robot->num_dof);
  if(this->isTimeValid(t))
  {
    double prod;
    MatrixXd dprod(1,this->robot->num_dof);
    this->evalOrientationProduct(prod,dprod);
    c(0) = prod*prod;
    dc = 2.0*prod*dprod;
  }
  else
  {
    c.resize(0);
    dc.resize(0,0);
  }
}

void QuatConstraint::bounds(double* t, VectorXd &lb, VectorXd &ub)
{
  lb.resize(this->getNumConstraint(t));
  ub.resize(this->getNumConstraint(t));
  if(this->isTimeValid(t))
  {
    lb[0] = 1.0-this->tol;
    ub[0] = 1.0;
  }
}

QuatConstraint::~QuatConstraint(void)
{
}

WorldQuatConstraint::WorldQuatConstraint(RigidBodyManipulator *model, int body, Vector4d quat_des, double tol, Vector2d tspan):QuatConstraint(model,tol,tspan)
{
  this->body = body;
  this->body_name = this->robot->bodies[this->body].linkname;
  assert(quat_des.norm()>0);
  quat_des = quat_des/quat_des.norm();
  this->quat_des = quat_des;
}

void WorldQuatConstraint::evalOrientationProduct(double &prod, MatrixXd &dprod)
{
  Matrix<double,7,1>  x;
  MatrixXd J(7,this->robot->num_dof);
  Vector4d pts;
  pts << 0.0,0.0,0.0,1.0;
  this->robot->forwardKin(this->body,pts,2,x);
  this->robot->forwardJac(this->body,pts,2,J);
  Vector4d quat = x.tail(4);
  prod = (quat.transpose()*this->quat_des);
  dprod = this->quat_des.transpose()*J.block(3,0,4,this->robot->num_dof);
}


void WorldQuatConstraint::name(double* t, std::vector<std::string> &name_str)
{
  if(this->isTimeValid(t))
  {
    char cnst_name_str_buffer[500]; 
    if(t == NULL)
    {
      sprintf(cnst_name_str_buffer,"%s quaternion constraint",this->body_name.c_str());
    }
    else
    {
      sprintf(cnst_name_str_buffer,"%s quaternion constraint at time %10.4f",this->body_name.c_str(),*t);
    }
    std::string cnst_name_str(cnst_name_str_buffer);
    for(int i = 0;i<this->num_constraint;i++)
    {
      name_str.push_back(cnst_name_str);
    }
  }
}

void WorldQuatConstraint::updateRobot(RigidBodyManipulator* robot)
{
  this->robot = robot;
}

WorldQuatConstraint::~WorldQuatConstraint()
{
}

EulerConstraint::EulerConstraint(RigidBodyManipulator *model, Vector3d lb, Vector3d ub, Vector2d tspan):SingleTimeKinematicConstraint(model,tspan)
{
  this->num_constraint = 0;
  for(int i = 0;i<3;i++)
  {
    if(mxIsNaN(lb(i)))
    {
      lb(i) = -mxGetInf();
    }
    if(mxIsNaN(ub(i)))
    {
      ub(i) = mxGetInf();
    }
    if(ub(i)<lb(i))
    {
      mexErrMsgIdAndTxt("Drake:EulerConstraint:BadInputs","lb must be no larger than ub");
    }
    if(mxIsInf(lb(i))&&mxIsInf(ub(i)))
    {
      null_constraint_rows[i] = true;
    }
    else
    {
      null_constraint_rows[i] = false;
      this->num_constraint++;
    }
  }
  this->lb = new double[this->num_constraint];
  this->ub = new double[this->num_constraint];
  int valid_row_idx = 0;
  int bnd_idx = 0;
  while(bnd_idx<this->num_constraint)
  {
    if(!this->null_constraint_rows[valid_row_idx])
    {
      this->lb[bnd_idx] = lb(valid_row_idx);
      this->ub[bnd_idx] = ub(valid_row_idx);
      bnd_idx++;
      valid_row_idx++;
    }
    else
    {
      valid_row_idx++;
    }
  }
  this->avg_rpy = new double[num_constraint];
  for(int i = 0;i<num_constraint;i++)
  {
    this->avg_rpy[i] = (this->lb[i]+this->ub[i])/2.0;
  }
}

void EulerConstraint::eval(double* t, VectorXd &c, MatrixXd &dc)
{
  int n_constraint = this->getNumConstraint(t);
  if(this->isTimeValid(t))
  {
    Vector3d rpy;
    MatrixXd drpy(3,this->robot->num_dof);
    this->evalrpy(rpy,drpy);
    c.resize(n_constraint);
    dc.resize(n_constraint,this->robot->num_dof);
    int valid_row_idx = 0;
    int i = 0;
    while(i<n_constraint)
    {
      if(!this->null_constraint_rows[valid_row_idx])
      {
        c(i) = rpy(valid_row_idx);
        c(i) = angleDiff(this->avg_rpy[i],c(i))+this->avg_rpy[i];
        dc.row(i) = drpy.row(valid_row_idx);
        valid_row_idx++;
        i++;
      }
      else
      {
        valid_row_idx++;
      }
    }
  }
  else
  {
    c.resize(0);
    dc.resize(0,0);
  }
}

void EulerConstraint::bounds(double* t, VectorXd &lb, VectorXd &ub)
{
  lb.resize(this->getNumConstraint(t));
  ub.resize(this->getNumConstraint(t));
  if(this->isTimeValid(t))
  {
    memcpy(lb.data(),this->lb,sizeof(double)*this->num_constraint);
    memcpy(ub.data(),this->ub,sizeof(double)*this->num_constraint);
  }
}

EulerConstraint::~EulerConstraint()
{
  delete[] ub;
  delete[] lb;
}

WorldEulerConstraint::WorldEulerConstraint(RigidBodyManipulator *model, int body, Vector3d lb, Vector3d ub, Vector2d tspan): EulerConstraint(model,lb,ub,tspan)
{
  this->body = body;
  this->body_name = this->robot->bodies[body].linkname;
}

void WorldEulerConstraint::evalrpy(Vector3d &rpy,MatrixXd &J)
{
  Vector4d pt;
  pt<<0.0,0.0,0.0,1.0;
  Matrix<double,6,1> x;
  MatrixXd dx(6,this->robot->num_dof);
  this->robot->forwardKin(this->body,pt,1,x);
  this->robot->forwardJac(this->body,pt,1,dx);
  rpy = x.tail(3);
  J = dx.block(3,0,3,this->robot->num_dof);
}

void WorldEulerConstraint::name(double* t, std::vector<std::string> &name_str)
{
  if(this->isTimeValid(t))
  {
    char cnst_name_str_buffer[500]; 
    int constraint_idx = 0;
    if(!this->null_constraint_rows[0])
    {
      if(t == NULL)
      {
        sprintf(cnst_name_str_buffer,"%s roll",this->body_name.c_str());
      }
      else
      {
        sprintf(cnst_name_str_buffer,"%s roll at time %10.4f",this->body_name.c_str(),*t);
      }
      std::string cnst_name_str(cnst_name_str_buffer);
      name_str.push_back(cnst_name_str);
      constraint_idx++;
    }
    if(!this->null_constraint_rows[1])
    {
      if(t == NULL)
      {
        sprintf(cnst_name_str_buffer,"%s pitch",this->body_name.c_str());
      }
      else
      {
        sprintf(cnst_name_str_buffer,"%s pitch at time %10.4f",this->body_name.c_str(),*t);
      }
      std::string cnst_name_str(cnst_name_str_buffer);
      name_str.push_back(cnst_name_str);
      constraint_idx++;
    }
    if(!this->null_constraint_rows[2])
    {
      if(t == NULL)
      {
        sprintf(cnst_name_str_buffer,"%s yaw",this->body_name.c_str());
      }
      else
      {
        sprintf(cnst_name_str_buffer,"%s yaw at time %10.4f",this->body_name.c_str(),*t);
      }
      std::string cnst_name_str(cnst_name_str_buffer);
      name_str.push_back(cnst_name_str);
      constraint_idx++;
    }
  }
}

void WorldEulerConstraint::updateRobot(RigidBodyManipulator* robot)
{
  this->robot = robot;
}

WorldEulerConstraint::~WorldEulerConstraint()
{
}

GazeConstraint::GazeConstraint(RigidBodyManipulator* model, Vector3d axis, double conethreshold, Vector2d tspan):SingleTimeKinematicConstraint(model,tspan)
{
  double len_axis = axis.norm();
  assert(len_axis>0);
  this->axis = axis/len_axis;
  assert(conethreshold>=0&&conethreshold<=M_PI+1E-10);
  this->conethreshold = conethreshold;
}

GazeOrientConstraint::GazeOrientConstraint(RigidBodyManipulator* model, Vector3d axis, Vector4d quat_des, double conethreshold, double threshold, Vector2d tspan): GazeConstraint(model,axis,conethreshold,tspan)
{
  double len_quat_des = quat_des.norm();
  assert(len_quat_des>0);
  this->quat_des = quat_des/len_quat_des;
  assert(threshold>=0&&threshold<=M_PI+1E-10);
  this->threshold = threshold;
  this->num_constraint = 2;
}

void GazeOrientConstraint::eval(double* t, VectorXd &c, MatrixXd &dc)
{
  int num_constraint = this->getNumConstraint(t);
  c.resize(num_constraint);
  dc.resize(num_constraint,this->robot->num_dof);
  if(this->isTimeValid(t))
  {
    Vector4d quat;
    int nq = this->robot->num_dof;
    MatrixXd dquat(4,nq);
    this->evalOrientation(quat,dquat);
    double axis_err;
    Matrix<double,1,11> daxis_err; 
    quatDiffAxisInvar(quat,this->quat_des,this->axis,axis_err,daxis_err);
    MatrixXd daxis_err_dq(1,nq);
    daxis_err_dq = daxis_err.block(0,0,1,4)*dquat;
    Vector4d q_diff;
    Matrix<double,4,8> dq_diff;
    quatDiff(quat,this->quat_des,q_diff,dq_diff);
    MatrixXd dq_diff_dq(4,nq);
    dq_diff_dq = dq_diff.block(0,0,4,4)*dquat;
    c << axis_err, q_diff(0);
    dc.row(0) = daxis_err_dq;
    dc.row(1) = dq_diff_dq.row(0);
  }
  else
  {
    c.resize(0);
    dc.resize(0,0);
  }
}

void GazeOrientConstraint::bounds(double* t, VectorXd &lb, VectorXd &ub)
{
  lb.resize(this->getNumConstraint(t));
  ub.resize(this->getNumConstraint(t));
  if(this->isTimeValid(t))
  {
    lb << cos(this->conethreshold)-1.0,cos(this->threshold/2.0);
    ub << 0, mxGetInf(); 
  }
}

WorldGazeOrientConstraint::WorldGazeOrientConstraint(RigidBodyManipulator* model, int body, Vector3d axis, Vector4d quat_des,double conethreshold, double threshold, Vector2d tspan): GazeOrientConstraint(model,axis,quat_des,conethreshold,threshold,tspan)
{
  this->body = body;
  this->body_name = this->robot->bodies[this->body].linkname;
}

void WorldGazeOrientConstraint::evalOrientation(Vector4d &quat, MatrixXd &dquat_dq)
{
  Matrix<double,7,1> x;
  MatrixXd J(7,this->robot->num_dof);
  Vector4d pts;
  pts<<0.0,0.0,0.0,1.0;
  this->robot->forwardKin(this->body,pts,2,x);
  this->robot->forwardJac(this->body,pts,2,J);
  quat = x.tail(4);
  dquat_dq = J.block(3,0,4,this->robot->num_dof);
}


void WorldGazeOrientConstraint::name(double* t, std::vector<std::string> &name_str)
{
  if(this->isTimeValid(t))
  {
    char cnst_name_str_buffer[500]; 
    std::string cnst_name_str;
    if(t == NULL)
    {
      sprintf(cnst_name_str_buffer,"%s conic gaze orientation constraint",this->body_name.c_str());
      cnst_name_str = std::string(cnst_name_str_buffer);
      name_str.push_back(cnst_name_str);
      sprintf(cnst_name_str_buffer,"%s revolute gaze orientation constraint",this->body_name.c_str());
      cnst_name_str = std::string(cnst_name_str_buffer);
      name_str.push_back(cnst_name_str);
    }
    else
    {
      sprintf(cnst_name_str_buffer,"%s conic gaze orientation constraint at time %10.4f",this->body_name.c_str(),*t);
      cnst_name_str = std::string(cnst_name_str_buffer);
      name_str.push_back(cnst_name_str);
      sprintf(cnst_name_str_buffer,"%s revolute gaze orientation constraint at time %10.4f",this->body_name.c_str(),*t);
      cnst_name_str = std::string(cnst_name_str_buffer);
      name_str.push_back(cnst_name_str);
    }
  }
}

void WorldGazeOrientConstraint::updateRobot(RigidBodyManipulator* robot)
{
  this->robot = robot;
}

GazeDirConstraint::GazeDirConstraint(RigidBodyManipulator* model, Vector3d axis, Vector3d dir, double conethreshold, Vector2d tspan):GazeConstraint(model,axis,conethreshold,tspan)
{
  double len_dir = dir.norm();
  assert(len_dir>0);
  this->dir = dir/len_dir;
  this->num_constraint = 1;
}

void GazeDirConstraint::eval(double* t, VectorXd &c, MatrixXd &dc)
{
  int num_constraint = this->getNumConstraint(t);
  c.resize(num_constraint);
  dc.resize(num_constraint,this->robot->num_dof);
  if(this->isTimeValid(t))
  {
    Vector4d quat;
    int nq = this->robot->num_dof;
    MatrixXd dquat(4,nq);
    this->evalOrientation(quat,dquat);
    Vector4d quat_des;
    quatTransform(this->axis,this->dir,quat_des);
    double axis_err;
    Matrix<double,1,11> daxis_err;
    quatDiffAxisInvar(quat,quat_des,this->axis,axis_err,daxis_err);
    c[0] = axis_err;
    dc.row(0) = daxis_err.head(4)*dquat;
  }
  else
  {
    c.resize(0);
    dc.resize(0,0);
  }
}

void GazeDirConstraint::bounds(double* t, VectorXd &lb, VectorXd &ub)
{
  int num_constraint = this->getNumConstraint(t);
  lb.resize(num_constraint);
  ub.resize(num_constraint);
  if(this->isTimeValid(t))
  {
    lb[0] = cos(this->conethreshold)-1.0;
    ub[0] = 0.0;
  }
}

WorldGazeDirConstraint::WorldGazeDirConstraint(RigidBodyManipulator *model, int body, Vector3d axis, Vector3d dir, double conethreshold, Vector2d tspan): GazeDirConstraint(model,axis,dir,conethreshold,tspan)
{
  this->body = body;
  this->body_name = this->robot->bodies[this->body].linkname;
}

void WorldGazeDirConstraint::evalOrientation(Vector4d &quat, MatrixXd &dquat_dq)
{
  Matrix<double,7,1> x;
  MatrixXd J(7,this->robot->num_dof);
  Vector4d pts;
  pts<< 0.0, 0.0, 0.0, 1.0;
  this->robot->forwardKin(this->body,pts,2,x);
  this->robot->forwardJac(this->body,pts,2,J);
  quat = x.tail(4);
  dquat_dq = J.block(3,0,4,this->robot->num_dof);
}

void WorldGazeDirConstraint::name(double* t, std::vector<std::string> &name_str)
{
  if(this->isTimeValid(t))
  {
    char cnst_name_str_buffer[500]; 
    if(t == NULL)
    {
      sprintf(cnst_name_str_buffer,"%s conic gaze direction constraint",this->body_name.c_str());
    }
    else
    {
      sprintf(cnst_name_str_buffer,"%s conic gaze direction constraint at time %10.4f",this->body_name.c_str(),*t);
    }
    std::string cnst_name_str(cnst_name_str_buffer);
    name_str.push_back(cnst_name_str);
  }
}

void WorldGazeDirConstraint::updateRobot(RigidBodyManipulator* robot)
{
  this->robot = robot;
}

GazeTargetConstraint::GazeTargetConstraint(RigidBodyManipulator* model, Vector3d axis, Vector3d target, Vector4d gaze_origin, double conethreshold, Vector2d tspan):GazeConstraint(model,axis,conethreshold,tspan)
{
  this->target = target;
  this->gaze_origin = gaze_origin;
  this->num_constraint = 1;
} 

void GazeTargetConstraint::bounds(double* t, VectorXd &lb, VectorXd &ub)
{
  int num_constraint = this->getNumConstraint(t);
  lb.resize(num_constraint);
  ub.resize(num_constraint);
  if(this->isTimeValid(t))
  {
    lb[0] = cos(this->conethreshold)-1.0;
    ub[0] = 0.0;
  }
}

WorldGazeTargetConstraint::WorldGazeTargetConstraint(RigidBodyManipulator* model, int body, Vector3d axis, Vector3d target, Vector4d gaze_origin, double conethreshold, Vector2d tspan): GazeTargetConstraint(model,axis,target,gaze_origin,conethreshold,tspan)
{
  this->body = body;
  this->body_name = this->robot->bodies[body].linkname;
}

void WorldGazeTargetConstraint::eval(double* t,VectorXd &c, MatrixXd &dc)
{
  int num_constraint = this->getNumConstraint(t);
  int nq = this->robot->num_dof;
  c.resize(num_constraint);
  dc.resize(num_constraint,nq);
  if(this->isTimeValid(t))
  {
    Matrix<double,7,1> x;
    MatrixXd J(7,nq);
    this->robot->forwardKin(this->body,this->gaze_origin,2,x);
    this->robot->forwardJac(this->body,this->gaze_origin,2,J);
    Vector3d gaze_vec = this->target-x.head(3);
    double len_gaze_vec = gaze_vec.norm();
    MatrixXd dlen_gaze_vec(1,nq);
    dlen_gaze_vec = -gaze_vec.transpose()*J.block(0,0,3,nq)/len_gaze_vec;
    MatrixXd dgaze_vec(3,nq);
    dgaze_vec = (-J.block(0,0,3,nq)*len_gaze_vec-gaze_vec*dlen_gaze_vec)/(len_gaze_vec*len_gaze_vec);
    gaze_vec = gaze_vec/len_gaze_vec;
    Vector4d quat_des;
    Matrix<double,4,6> dquat_des;
    quatTransform(gaze_vec,this->axis,quat_des,dquat_des);
    MatrixXd dquat_des_dq(4,nq);
    dquat_des_dq = dquat_des.block(0,0,4,3)*dgaze_vec;
    double axis_err;
    Matrix<double,1,11> daxis_err;
    quatDiffAxisInvar(x.tail(4),quat_des,this->axis,axis_err,daxis_err);
    MatrixXd daxis_err_dq(1,nq);
    daxis_err_dq = daxis_err.head(4)*J.block(3,0,4,nq)+daxis_err.segment(4,4)*dquat_des_dq;
    c(0) = axis_err;
    dc.row(0) = daxis_err_dq;
  }
  else
  {
    c.resize(0);
    dc.resize(0,0);
  }
}

void WorldGazeTargetConstraint::name(double* t, std::vector<std::string> &name_str)
{
  if(this->isTimeValid(t))
  {
    char cnst_name_str_buffer[500]; 
    if(t == NULL)
    {
      sprintf(cnst_name_str_buffer,"%s conic gaze target constraint",this->body_name.c_str());
    }
    else
    {
      sprintf(cnst_name_str_buffer,"%s conic gaze target constraint at time %10.4f",this->body_name.c_str(),*t);
    }
    std::string cnst_name_str(cnst_name_str_buffer);
    name_str.push_back(cnst_name_str);
  }
}

void WorldGazeTargetConstraint::updateRobot(RigidBodyManipulator* robot)
{
  this->robot = robot;
}

Point2PointDistanceConstraint::Point2PointDistanceConstraint(RigidBodyManipulator *robot, int bodyA, int bodyB, const MatrixXd &ptA, const MatrixXd &ptB, const VectorXd &lb, const VectorXd &ub, const Vector2d &tspan): SingleTimeKinematicConstraint(robot,tspan)
{
  this->bodyA = bodyA;
  this->bodyB = bodyB;
  this->ptA = ptA;
  this->ptB = ptB;
  this->num_constraint = ptA.cols();
  this->lb_square = lb.cwiseProduct(lb);
  this->ub_square = ub.cwiseProduct(ub);
}

void Point2PointDistanceConstraint::eval(double* t, VectorXd &c, MatrixXd &dc)
{
  if(this->isTimeValid(t))
  {
    int num_cnst = this->getNumConstraint(t);
    MatrixXd posA(3,this->ptA.cols());
    MatrixXd dposA(3*this->ptA.cols(),this->robot->num_dof);
    if(this->bodyA != -1)
    {
      this->robot->forwardKin(this->bodyA,this->ptA,0,posA);
      this->robot->forwardJac(this->bodyA,this->ptA,0,dposA);
    }
    else
    {
      posA = this->ptA.block(0,0,3,this->ptA.cols());
      dposA = MatrixXd::Zero(3*this->ptA.cols(),this->robot->num_dof);
    }
    MatrixXd posB(3,this->ptB.cols());
    MatrixXd dposB(3*this->ptB.cols(),this->robot->num_dof);
    if(this->bodyB != -1)
    {
      this->robot->forwardKin(this->bodyB,this->ptB,0,posB);
      this->robot->forwardJac(this->bodyB,this->ptB,0,dposB);
    }
    else
    {
      posB = this->ptB.block(0,0,3,this->ptB.cols());
      dposB = MatrixXd::Zero(3*this->ptB.cols(),this->robot->num_dof);
    }
    MatrixXd d = posA-posB;
    MatrixXd dd = dposA-dposB;
    MatrixXd tmp1 = d.cwiseProduct(d);
    MatrixXd tmp2 = tmp1.colwise().sum();
    c.resize(num_cnst,1);
    c = tmp2.transpose();
    dc.resize(num_cnst,this->robot->num_dof);
    for(int i = 0;i<num_cnst;i++)
    {
      dc.row(i) = 2*d.col(i).transpose()*dd.block(3*i,0,3,this->robot->num_dof);
    }
  }
  else
  {
    c.resize(0);
    dc.resize(0,0);
  }
}

void Point2PointDistanceConstraint::name(double* t, std::vector<std::string> &name_str)
{
  if(this->isTimeValid(t))
  {
    int num_cnst = this->getNumConstraint(t);
    for(int i = 0;i<num_cnst;i++)
    {
      char cnst_name_buffer[1000];
      std::string bodyA_name;
      if(this->bodyA != -1)
      {
        bodyA_name = this->robot->bodies[bodyA].linkname;
      }
      else
      {
        bodyA_name = "World";
      }
      std::string bodyB_name;
      if(this->bodyB != -1)
      {
        bodyB_name = this->robot->bodies[bodyB].linkname;
      }
      else
      {
        bodyB_name = "World";
      }
      sprintf(cnst_name_buffer,"Distance from %s pt %d to %s pt %d",bodyA_name.c_str(),i,bodyB_name.c_str(),i);
      std::string cnst_name_str(cnst_name_buffer);
      name_str.push_back(cnst_name_str);
    }
  }
}

void Point2PointDistanceConstraint::bounds(double* t, VectorXd &lb, VectorXd &ub)
{
  if(this->isTimeValid(t))
  {
    lb = this->lb_square;
    ub = this->ub_square;
  }
  else
  {
    lb.resize(0);
    ub.resize(0);
  }
}

void Point2PointDistanceConstraint::updateRobot(RigidBodyManipulator *robot)
{
  this->robot = robot;
}

WorldFixedPositionConstraint::WorldFixedPositionConstraint(RigidBodyManipulator* robot, int body, const MatrixXd &pts, const Vector2d &tspan):MultipleTimeKinematicConstraint(robot,tspan)
{
  this->body = body;
  assert(pts.rows() == 4);
  this->pts = pts;
  this->body_name = this->robot->bodies[body].linkname;
}

int WorldFixedPositionConstraint::getNumConstraint(double* t, int n_breaks)
{
  int num_valid_t = this->numValidTime(t,n_breaks);
  if(num_valid_t>=2)
  {
    return this->pts.cols();
  }
  else
  {
    return 0;
  }
}

void WorldFixedPositionConstraint::eval_valid(double* valid_t, int num_valid_t, const MatrixXd &valid_q, VectorXd &c, MatrixXd &dc_valid)
{
  int n_pts = this->pts.cols();
  int nq = this->robot->num_dof;
  MatrixXd *pos = new MatrixXd[num_valid_t];
  MatrixXd *dpos = new MatrixXd[num_valid_t];
  for(int i = 0;i<num_valid_t;i++)
  {
    this->robot->doKinematics((double*) valid_q.data()+i*nq);
    pos[i].resize(3,n_pts);
    this->robot->forwardKin(this->body,this->pts,0,pos[i]);
    dpos[i].resize(3*n_pts,nq);
    this->robot->forwardJac(this->body,this->pts,0,dpos[i]);
  }
  int* next_idx = new int[num_valid_t];
  int* prev_idx = new int[num_valid_t];
  for(int i = 0;i<num_valid_t;i++)
  {
    next_idx[i] = (i+1)%num_valid_t;
    prev_idx[i] = (i+num_valid_t-1)%num_valid_t;
  }
  c.resize(n_pts,1);
  dc_valid = MatrixXd::Zero(n_pts,nq*num_valid_t);
  for(int i = 0;i<num_valid_t;i++)
  {
    MatrixXd tmp1(3,n_pts);
    tmp1 = pos[i]-pos[next_idx[i]];
    MatrixXd tmp2 = tmp1.cwiseProduct(tmp1);
    VectorXd tmp3 = tmp2.colwise().sum();
    c += tmp3.transpose();
    for(int j = 0;j<n_pts;j++)
    {
      Vector3d tmp_vec = 4*pos[i].col(j)-2*pos[next_idx[i]].col(j)-2*pos[prev_idx[i]].col(j);
      dc_valid.block(j,i*nq,1,nq) = tmp_vec.transpose()*dpos[i].block(j*3,0,3,nq);
    }
  }
  delete[] pos;
  delete[] dpos;
  delete[] next_idx;
  delete[] prev_idx;
}

void WorldFixedPositionConstraint::bounds(double* t, int n_breaks, VectorXd &lb, VectorXd &ub)
{
  int num_valid_t = this->numValidTime(t,n_breaks);
  if(num_valid_t>=2)
  {
    int n_pts = this->pts.cols();
    lb.resize(n_pts,1);
    ub.resize(n_pts,1);
    lb = VectorXd::Zero(n_pts);
    ub = VectorXd::Zero(n_pts);
  }
  else
  {
    lb.resize(0);
    ub.resize(0);
  }
}

void WorldFixedPositionConstraint::name(double* t, int n_breaks,std::vector<std::string> &name_str)
{
  int num_valid_t = this->numValidTime(t,n_breaks);
  if(num_valid_t>=2)
  {
    int n_pts = this->pts.cols();
    for(int i = 0;i<n_pts;i++)
    {
      char cnst_name_buffer[500];
      sprintf(cnst_name_buffer,"World fixed position constraint for %s %ds point",this->body_name.c_str(),i);
      std::string cnst_name_str(cnst_name_buffer);
      name_str.push_back(cnst_name_str);
    }
  }
}

void WorldFixedPositionConstraint::updateRobot(RigidBodyManipulator* robot)
{
  this->robot = robot;
}

WorldFixedOrientConstraint::WorldFixedOrientConstraint(RigidBodyManipulator* robot, int body, const Vector2d &tspan): MultipleTimeKinematicConstraint(robot,tspan)
{
  this->body = body;
  this->body_name = this->robot->bodies[body].linkname;
}

int WorldFixedOrientConstraint::getNumConstraint(double* t, int n_breaks)
{
  int num_valid_t = this->numValidTime(t,n_breaks);
  if(num_valid_t>=2)
    return 1;
  else
    return 0;
}

void WorldFixedOrientConstraint::eval_valid(double* valid_t, int num_valid_t, const MatrixXd &valid_q, VectorXd &c, MatrixXd &dc_valid)
{
  int nq = this->robot->num_dof;
  Vector4d* quat = new Vector4d[num_valid_t];
  MatrixXd* dquat = new MatrixXd[num_valid_t];
  Vector4d origin_pt;
  origin_pt<<0.0,0.0,0.0,1.0;
  for(int i = 0;i<num_valid_t;i++)
  {
    this->robot->doKinematics((double*) valid_q.data()+i*nq);
    Matrix<double,7,1> tmp_pos;
    MatrixXd dtmp_pos(7,nq);
    this->robot->forwardKin(this->body,origin_pt,2,tmp_pos);
    this->robot->forwardJac(this->body,origin_pt,2,dtmp_pos);
    quat[i] = tmp_pos.tail(4);
    dquat[i].resize(4,nq);
    dquat[i] = dtmp_pos.block(3,0,4,nq);
  }
  int* next_idx = new int[num_valid_t];
  int* prev_idx = new int[num_valid_t];
  for(int i = 0;i<num_valid_t;i++)
  {
    next_idx[i] = (i+1)%num_valid_t;
    prev_idx[i] = (i+num_valid_t-1)%num_valid_t;
  }
  c(0) = 0.0;
  dc_valid = MatrixXd::Zero(1,nq*num_valid_t);
  for(int i = 0;i<num_valid_t;i++)
  {

    double tmp1 = quat[i].transpose()*quat[next_idx[i]];
    double tmp2 = quat[i].transpose()*quat[prev_idx[i]];
    c(0) += pow(tmp1,2);
    dc_valid.block(0,nq*i,1,nq) = (2*tmp1*quat[next_idx[i]].transpose() + 2*tmp2*quat[prev_idx[i]].transpose())*dquat[i];
  }
  delete[] quat;
  delete[] dquat;
  delete[] next_idx;
  delete[] prev_idx;
}

void WorldFixedOrientConstraint::bounds(double* t,int n_breaks, VectorXd &lb, VectorXd &ub)
{
  int num_valid_t = this->numValidTime(t,n_breaks);
  if(num_valid_t>=2)
  {
    lb.resize(1);
    ub.resize(1);
    lb(0) = (double) num_valid_t;
    ub(0) = (double) num_valid_t;
  }
  else
  {
    lb.resize(0);
    ub.resize(0);
  }
}

void WorldFixedOrientConstraint::name(double* t, int n_breaks, std::vector<std::string> &name_str)
{
  int num_valid_t = this->numValidTime(t,n_breaks);
  if(num_valid_t>=2)
  {
    char cnst_name_buffer[500];
    sprintf(cnst_name_buffer,"World fixed orientation constraint for %s",this->body_name.c_str());
    std::string cnst_name_str(cnst_name_buffer);
    name_str.push_back(cnst_name_str);
  }
}

void WorldFixedOrientConstraint::updateRobot(RigidBodyManipulator* robot)
{
  this->robot = robot;
}

WorldFixedBodyPoseConstraint::WorldFixedBodyPoseConstraint(RigidBodyManipulator *robot, int body, const Vector2d &tspan): MultipleTimeKinematicConstraint(robot,tspan)
{
  this->body = body;
  this->body_name = this->robot->bodies[this->body].linkname;
}

int WorldFixedBodyPoseConstraint::getNumConstraint(double* t, int n_breaks)
{
  int num_valid_t = this->numValidTime(t,n_breaks);
  if(num_valid_t>=2)
  {
    return 2;
  }
  else
  {
    return 0;
  }
}

void WorldFixedBodyPoseConstraint::eval_valid(double* valid_t, int num_valid_t,const MatrixXd &valid_q, VectorXd &c, MatrixXd &dc_valid)
{
  int nq = this->robot->num_dof;
  Vector3d *pos = new Vector3d[num_valid_t];
  Vector4d *quat = new Vector4d[num_valid_t];
  MatrixXd *dpos = new MatrixXd[num_valid_t];
  MatrixXd *dquat = new MatrixXd[num_valid_t];
  Vector4d origin_pt;
  origin_pt<< 0.0,0.0,0.0,1.0;
  for(int i = 0;i<num_valid_t;i++)
  {
    this->robot->doKinematics((double*) valid_q.data()+i*nq);
    Matrix<double,7,1> pos_tmp;
    this->robot->forwardKin(this->body,origin_pt,2,pos_tmp);
    pos[i] = pos_tmp.head(3);
    quat[i] = pos_tmp.tail(4);
    MatrixXd J_tmp(7,nq);
    this->robot->forwardJac(this->body,origin_pt,2,J_tmp);
    dpos[i].resize(3,nq);
    dpos[i] = J_tmp.block(0,0,3,nq);
    dquat[i].resize(4,nq);
    dquat[i] = J_tmp.block(3,0,4,nq);
  }
  int* next_idx = new int[num_valid_t];
  int* prev_idx = new int[num_valid_t];
  for(int i = 0;i<num_valid_t;i++)
  {
    next_idx[i] = (i+1)%num_valid_t;
    prev_idx[i] = (i+num_valid_t-1)%num_valid_t;
  }
  c = Vector2d::Zero();
  dc_valid = MatrixXd::Zero(2,nq*num_valid_t);
  for(int i = 0;i<num_valid_t;i++)
  {
    Vector3d tmp1;
    tmp1 = pos[i]-pos[next_idx[i]];
    c(0) += tmp1.transpose()*tmp1;
    Vector3d tmp_vec = 4*pos[i]-2*pos[next_idx[i]]-2*pos[prev_idx[i]];
    dc_valid.block(0,i*nq,1,nq) = tmp_vec.transpose()*dpos[i];
    double tmp2 = quat[i].transpose()*quat[next_idx[i]];
    double tmp3 = quat[i].transpose()*quat[prev_idx[i]];
    c(1) += pow(tmp2,2);
    dc_valid.block(1,nq*i,1,nq) = (2*tmp2*quat[next_idx[i]].transpose() + 2*tmp3*quat[prev_idx[i]].transpose())*dquat[i];
  }
  delete[] pos;
  delete[] dpos;
  delete[] quat;
  delete[] dquat;
  delete[] next_idx;
  delete[] prev_idx;
}

void WorldFixedBodyPoseConstraint::bounds(double* t, int n_breaks, VectorXd &lb, VectorXd &ub)
{
  int num_valid_t = this->numValidTime(t,n_breaks);
  if(num_valid_t>=2)
  {
    lb.resize(2);
    ub.resize(2);
    lb<<0.0,(double) num_valid_t;
    ub<<0.0,(double) num_valid_t;
  }
  else
  {
    lb.resize(0);
    ub.resize(0);
  }
}

void WorldFixedBodyPoseConstraint::name(double* t, int n_breaks, std::vector<std::string> &name_str)
{
  int num_valid_t = this->numValidTime(t,n_breaks);
  if(num_valid_t>=2)
  {
    char cnst_name_buffer1[500];
    sprintf(cnst_name_buffer1,"World fixed body pose constraint for %s position",this->body_name.c_str());
    std::string cnst_name_str1(cnst_name_buffer1);
    name_str.push_back(cnst_name_str1);
    char cnst_name_buffer2[100];
    sprintf(cnst_name_buffer2,"World fixed body pose constraint for %s orientation",this->body_name.c_str());
    std::string cnst_name_str2(cnst_name_buffer2);
    name_str.push_back(cnst_name_str2);
  }
}

void WorldFixedBodyPoseConstraint::updateRobot(RigidBodyManipulator* robot)
{
  this->robot = robot;
}

AllBodiesClosestDistanceConstraint::AllBodiesClosestDistanceConstraint(
    RigidBodyManipulator* model, double lb, double ub, Vector2d tspan)
  : SingleTimeKinematicConstraint(model, tspan), lb(lb), ub(ub)
{
  VectorXd c;
  MatrixXd dc;
  double t = 0;
  eval(&t,c,dc);
  //DEBUG
  //std::cout << "ABCDC::ABCDC: c.size() = " << c.size() << std::endl;
  //END_DEBUG
  num_constraint = c.size();
};

void 
AllBodiesClosestDistanceConstraint::eval(double* t, VectorXd& c, MatrixXd& dc)
{
  robot->closestDistanceAllBodies(c,dc);
  //DEBUG
  //std::cout << "ABCDC::eval: c.size() = " << c.size() << std::endl;
  //END_DEBUG
};

void AllBodiesClosestDistanceConstraint::bounds(double* t, VectorXd& lb, VectorXd& ub)
{
  lb.resize(num_constraint);
  ub.resize(num_constraint);
  if(this->isTimeValid(t))
  {
    lb = VectorXd::Constant(num_constraint,this->lb);
    ub = VectorXd::Constant(num_constraint,this->ub);
  }
}

void AllBodiesClosestDistanceConstraint::name(double* t, std::vector<std::string> &name)
{
  if(this->isTimeValid(t))
  {
    char cnst_name_buffer[100]; 
    if(t == NULL)
    {
      sprintf(cnst_name_buffer,"All-to-all closest distance constraint");
    }
    else
    {
      sprintf(cnst_name_buffer,
          "All-to-all closest distance constraint at time %10.4f",*t);
    }
    std::string cnst_name(cnst_name_buffer);
    for(int i = 0;i<this->num_constraint;i++)
    {
      name.push_back(cnst_name);
    }
  }
  else
  {
    name.push_back("");
  }
}

void AllBodiesClosestDistanceConstraint::updateRobot(RigidBodyManipulator* robot)
{
  this->robot = robot;
}

WorldPositionInFrameConstraint::WorldPositionInFrameConstraint(
    RigidBodyManipulator *model, int body, const Eigen::MatrixXd &pts, 
    const Eigen::Matrix4d& T_frame_to_world, 
    Eigen::MatrixXd lb, Eigen::MatrixXd ub, const Eigen::Vector2d &tspan)
  : WorldPositionConstraint(model,body,pts,lb,ub,tspan)
{
  this->T_frame_to_world = T_frame_to_world;
  this->T_world_to_frame = T_frame_to_world.inverse();
}

void WorldPositionInFrameConstraint::evalPositions(MatrixXd &pos, MatrixXd &J)
{
  WorldPositionConstraint::evalPositions(pos, J);
  MatrixXd pos_1(4,n_pts);
  pos_1 << pos, MatrixXd::Ones(1,n_pts);
  pos = (this->T_world_to_frame*pos_1).topRows(3);
  auto J_reshaped = Map<MatrixXd>(J.data(),3,n_pts*J.cols());
  J_reshaped = T_world_to_frame.topLeftCorner<3,3>()*J_reshaped;
}

WorldPositionInFrameConstraint::~WorldPositionInFrameConstraint()
{
}