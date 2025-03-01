#include "../../../include/crystalPlasticity.h"
#include <iostream>
#include <fstream>

template <int dim>
void crystalPlasticity<dim>::updateAfterIncrement()
{
	local_F_r=0.0;
	local_F_s=0.0;
	local_F_e = 0.0;
	QGauss<dim>  quadrature(this->userInputs.quadOrder);
	FEValues<dim> fe_values(this->FE, quadrature, update_quadrature_points | update_gradients | update_JxW_values);
	const unsigned int num_quad_points = quadrature.size();
	const unsigned int   dofs_per_cell = this->FE.dofs_per_cell;
	std::vector<unsigned int> local_dof_indices(dofs_per_cell);
	if (this->userInputs.flagTaylorModel){
		if(initCalled == false){
			if(this->userInputs.enableAdvancedTwinModel){
				init2(num_quad_points);
			}
			else{
				init(num_quad_points);
			}
		}
	}
	//loop over elements
	unsigned int cellID = 0;
	typename DoFHandler<dim>::active_cell_iterator cell = this->dofHandler.begin_active(), endc = this->dofHandler.end();
	for (; cell != endc; ++cell) {
		if (cell->is_locally_owned()) {
			fe_values.reinit(cell);
			//loop over quadrature points
			cell->set_user_index(fe_values.get_cell()->user_index());
			cell->get_dof_indices(local_dof_indices);
			Vector<double> Ulocal(dofs_per_cell);

			if (!this->userInputs.flagTaylorModel){
				for (unsigned int i = 0; i < dofs_per_cell; i++) {
					Ulocal[i] = this->solutionWithGhosts[local_dof_indices[i]];
				}
			}
			for (unsigned int q = 0; q < num_quad_points; ++q) {
				//Get deformation gradient
				F = 0.0;
				if (this->userInputs.flagTaylorModel){
					F =this->Fprev;
				}
				else{
					for (unsigned int d = 0; d < dofs_per_cell; ++d) {
						unsigned int i = fe_values.get_fe().system_to_component_index(d).first;
						for (unsigned int j = 0; j < dim; ++j) {
							F[i][j] += Ulocal(d)*fe_values.shape_grad(d, q)[j]; // u_{i,j}= U(d)*N(d)_{,j}, where d is the DOF correonding to the i'th dimension
						}
					}
					for (unsigned int i = 0; i < dim; ++i) {
						F[i][i] += 1;
					}
				}
				//Update strain, stress, and tangent for current time step/quadrature point
				calculatePlasticity(cellID, q, 0);

				FullMatrix<double> temp,temp3,temp4, C_tau(dim, dim), E_tau(dim, dim), b_tau(dim, dim);
				Vector<double> temp2;
				temp.reinit(dim, dim); temp = 0.0;
				temp2.reinit(dim); temp2 = 0.0;
				temp3.reinit(dim, dim); temp3 = 0.0;
				temp4.reinit(dim, dim); temp4 = 0.0;
				C_tau = 0.0;
				temp = F;
				F.Tmmult(C_tau, temp);
				F.mTmult(b_tau, temp);
				//E_tau = CE_tau;
				temp = IdentityMatrix(dim);
				for (unsigned int i = 0;i<dim;i++) {
					temp2[i] = 0.5*log(b_tau[i][i])*fe_values.JxW(q);
					for (unsigned int j = 0;j<dim;j++) {
						E_tau[i][j] = 0.5*(C_tau[i][j] - temp[i][j]);
						temp3[i][j] = T[i][j] * fe_values.JxW(q);
						temp4[i][j] = E_tau[i][j]*fe_values.JxW(q);
					}
				}

				CauchyStress[cellID][q]=T;


				if (this->userInputs.enableAdvRateDepModel){
					for(unsigned int i=0; i<dim ; i++){
						for(unsigned int j=0 ; j<dim ; j++){
							TinterStress_diff[cellID][q][i][j] = T_inter[i][j] - TinterStress[cellID][q][i][j] ;
							TinterStress[cellID][q][i][j] = T_inter[i][j];
						}
					}
				}

				local_strain.add(1.0, temp4);
				local_stress.add(1.0, temp3);
				local_microvol = local_microvol + fe_values.JxW(q);

				//calculate von-Mises stress and equivalent strain
				double traceE, traceT, vonmises, eqvstrain;
				FullMatrix<double> deve(dim, dim), devt(dim, dim);


				traceE = E_tau.trace();
				traceT = T.trace();
				temp = IdentityMatrix(3);
				temp.equ(traceE / 3, temp);

				deve = E_tau;
				deve.add(-1.0, temp);

				temp = IdentityMatrix(3);
				temp.equ(traceT / 3, temp);

				devt = T;
				devt.add(-1.0, temp);

				vonmises = devt.frobenius_norm();
				vonmises = sqrt(3.0 / 2.0)*vonmises;
				eqvstrain = deve.frobenius_norm();
				eqvstrain = sqrt(2.0 / 3.0)*eqvstrain;

				//fill in post processing field values
				if (!this->userInputs.enableAdvancedTwinModel){
					twin_ouput[cellID][q]=twin_iter[cellID][q];
				}
				else{
					if (TotaltwinvfK[cellID][q]>=this->userInputs.criteriaTwinVisual){
						twin_ouput[cellID][q]=1;
					}
					else{
						twin_ouput[cellID][q]=0;
					}
				}
				if (this->userInputs.writeOutput){
					this->postprocessValues(cellID, q, 0, 0) = vonmises;
					this->postprocessValues(cellID, q, 1, 0) = eqvstrain;
					this->postprocessValues(cellID, q, 2, 0) = twin_ouput[cellID][q];

					////////User Defined Variables for visualization outputs (output_Var1 to output_Var24)////////
					this->postprocessValues(cellID, q, 3, 0) = 0;
					this->postprocessValues(cellID, q, 4, 0) = 0;
					this->postprocessValues(cellID, q, 5, 0) = 0;
					this->postprocessValues(cellID, q, 6, 0) = 0;
					this->postprocessValues(cellID, q, 7, 0) = 0;
					this->postprocessValues(cellID, q, 8, 0) = 0;
					this->postprocessValues(cellID, q, 9, 0) = 0;
					this->postprocessValues(cellID, q, 10, 0) = 0;
					this->postprocessValues(cellID, q, 11, 0) = 0;
					this->postprocessValues(cellID, q, 12, 0) = 0;
					this->postprocessValues(cellID, q, 13, 0) = 0;
					this->postprocessValues(cellID, q, 14, 0) = 0;
					this->postprocessValues(cellID, q, 15, 0) = 0;
					this->postprocessValues(cellID, q, 16, 0) = 0;
					this->postprocessValues(cellID, q, 17, 0) = 0;
					this->postprocessValues(cellID, q, 18, 0) = 0;
					this->postprocessValues(cellID, q, 19, 0) = 0;
					this->postprocessValues(cellID, q, 20, 0) = 0;
					this->postprocessValues(cellID, q, 21, 0) = 0;
					this->postprocessValues(cellID, q, 22, 0) = 0;
					this->postprocessValues(cellID, q, 23, 0) = 0;
					this->postprocessValues(cellID, q, 24, 0) = 0;
					this->postprocessValues(cellID, q, 25, 0) = 0;
					this->postprocessValues(cellID, q, 26, 0) = 0;
				}



				for(unsigned int i=0;i<this->userInputs.numTwinSystems1;i++){
					local_F_r=local_F_r+twinfraction_iter[cellID][q][i]*fe_values.JxW(q);
				}

				if (!this->userInputs.enableAdvancedTwinModel){
					local_F_e = local_F_e + twin_ouput[cellID][q] * fe_values.JxW(q);
				}
				else{
					local_F_e=local_F_e+ TotaltwinvfK[cellID][q]*fe_values.JxW(q);
				}


				for(unsigned int i=0;i<this->userInputs.numSlipSystems1;i++){
					local_F_s=local_F_s+slipfraction_iter[cellID][q][i]*fe_values.JxW(q);
				}

			}
			if (this->userInputs.writeOutput){
				this->postprocessValuesAtCellCenters(cellID,0)=cellOrientationMap[cellID];
			}

			cellID++;
		}
	}

	//In Case we have twinning
	rotnew_conv=rotnew_iter;

	if (!this->userInputs.enableAdvancedTwinModel){
		//reorient() updates the rotnew_conv.
		reorient();
	}

	//Updating rotnew_iter using rotnew_conv updated by reorient();
	rotnew_iter=rotnew_conv;

	//Update the history variables when convergence is reached for the current increment
	Fe_conv=Fe_iter;
	Fp_conv=Fp_iter;
	s_alpha_conv=s_alpha_iter;
	W_kh_conv = W_kh_iter;
	twinfraction_conv=twinfraction_iter;
	slipfraction_conv=slipfraction_iter;
	rot_conv=rot_iter;
	twin_conv=twin_iter;

	if (this->userInputs.enableUserMaterialModel){
		stateVar_conv=stateVar_iter;
	}

	if (this->userInputs.enableAdvancedTwinModel){
		TwinMaxFlag_conv = TwinMaxFlag_iter;
		NumberOfTwinnedRegion_conv = NumberOfTwinnedRegion_iter;
		ActiveTwinSystems_conv = ActiveTwinSystems_iter;
		TwinFlag_conv = TwinFlag_iter;
		TwinOutputfraction_conv=TwinOutputfraction_iter;
	}





	char buffer[200];

	//////////////////////TabularOutput Start///////////////
	std::vector<unsigned int> tabularTimeInputIncInt;
	std::vector<double> tabularTimeInputInc;
	if (this->userInputs.tabularOutput){

		tabularTimeInputInc=this->userInputs.tabularTimeOutput;
		for(unsigned int i=0;i<this->userInputs.tabularTimeOutput.size();i++){
			tabularTimeInputInc[i]=tabularTimeInputInc[i]/this->delT;
		}

		tabularTimeInputIncInt.resize(this->userInputs.tabularTimeOutput.size(),0);
		///Converting to an integer always rounds down, even if the fraction part is 0.99999999.
		//Hence, I add 0.1 to make sure we always get the correct integer.
		for(unsigned int i=0;i<this->userInputs.tabularTimeOutput.size();i++){
			tabularTimeInputIncInt[i]=int(tabularTimeInputInc[i]+0.1);
		}
	}
	//////////////////////TabularOutput Finish///////////////
	if (this->userInputs.writeQuadratureOutput) {
		if (((!this->userInputs.tabularOutput)&&((this->currentIncrement+1)%this->userInputs.skipQuadratureOutputSteps == 0))||((this->userInputs.tabularOutput)&& (std::count(tabularTimeInputIncInt.begin(), tabularTimeInputIncInt.end(), (this->currentIncrement+1))==1))){
			//copy rotnew to output
			outputQuadrature.clear();
			//loop over elements
			cellID=0;
			cell = this->dofHandler.begin_active(), endc = this->dofHandler.end();
			for (; cell!=endc; ++cell) {
				if (cell->is_locally_owned()){
					fe_values.reinit(cell);
					//loop over quadrature points
					for (unsigned int q=0; q<num_quad_points; ++q){
						std::vector<double> temp;

						////////////GrainID of quadrature point////////////////
						temp.push_back(cellOrientationMap[cellID]);


            ////////////X,Y,Z (Position of the quadrature point)////////////////
						temp.push_back(fe_values.get_quadrature_points()[q][0]);
					  temp.push_back(fe_values.get_quadrature_points()[q][1]);
					  temp.push_back(fe_values.get_quadrature_points()[q][2]);

						if (this->userInputs.enableUserMaterialModel){
							////////////Plastic slip shear (Gamma) for each slip system. In the case of FCC, 12 values ////////////////
							temp.push_back(stateVar_conv[cellID][q][26]);
							temp.push_back(stateVar_conv[cellID][q][27]);
							temp.push_back(stateVar_conv[cellID][q][28]);
							temp.push_back(stateVar_conv[cellID][q][29]);
							temp.push_back(stateVar_conv[cellID][q][30]);
							temp.push_back(stateVar_conv[cellID][q][31]);
							temp.push_back(stateVar_conv[cellID][q][32]);
							temp.push_back(stateVar_conv[cellID][q][33]);
							temp.push_back(stateVar_conv[cellID][q][34]);
							temp.push_back(stateVar_conv[cellID][q][35]);
							temp.push_back(stateVar_conv[cellID][q][36]);
							temp.push_back(stateVar_conv[cellID][q][37]);
						////////////Normal stress for each slip system. In the case of FCC, 12 values ////////////////
							temp.push_back(stateVar_conv[cellID][q][39]);
							temp.push_back(stateVar_conv[cellID][q][40]);
							temp.push_back(stateVar_conv[cellID][q][41]);
							temp.push_back(stateVar_conv[cellID][q][42]);
							temp.push_back(stateVar_conv[cellID][q][43]);
							temp.push_back(stateVar_conv[cellID][q][44]);
							temp.push_back(stateVar_conv[cellID][q][45]);
							temp.push_back(stateVar_conv[cellID][q][46]);
							temp.push_back(stateVar_conv[cellID][q][47]);
							temp.push_back(stateVar_conv[cellID][q][48]);
							temp.push_back(stateVar_conv[cellID][q][49]);
							temp.push_back(stateVar_conv[cellID][q][50]);
							////////////Nine components of Plastic strain Tensor (Ep11, Ep12,Ep13,Ep21,Ep22,Ep23,Ep31,Ep32,Ep33) ////////////////
							temp.push_back(stateVar_conv[cellID][q][52]);
							temp.push_back(stateVar_conv[cellID][q][53]);
							temp.push_back(stateVar_conv[cellID][q][54]);
							temp.push_back(stateVar_conv[cellID][q][55]);
							temp.push_back(stateVar_conv[cellID][q][56]);
							temp.push_back(stateVar_conv[cellID][q][57]);
							temp.push_back(stateVar_conv[cellID][q][58]);
							temp.push_back(stateVar_conv[cellID][q][59]);
							temp.push_back(stateVar_conv[cellID][q][60]);
							////////////Effective Plastic Strain ////////////////
							temp.push_back(stateVar_conv[cellID][q][61]);
						}

						addToQuadratureOutput(temp);

					}
					cellID++;
				}
			}

			writeQuadratureOutput(this->userInputs.outputDirectory, this->currentIncrement);
		}
	}

	microvol=Utilities::MPI::sum(local_microvol,this->mpi_communicator);

	for(unsigned int i=0;i<dim;i++){
		for(unsigned int j=0;j<dim;j++){
			global_strain[i][j]=Utilities::MPI::sum(local_strain[i][j]/microvol,this->mpi_communicator);
			global_stress[i][j]=Utilities::MPI::sum(local_stress[i][j]/microvol,this->mpi_communicator);
		}
	}
	if (!this->userInputs.enableMultiphase){
		F_e = Utilities::MPI::sum(local_F_e / microvol, this->mpi_communicator);
		F_r=Utilities::MPI::sum(local_F_r/microvol,this->mpi_communicator);
		F_s=Utilities::MPI::sum(local_F_s/microvol,this->mpi_communicator);
	}
	else {
		F_e=0;F_r=0;F_s=0;
	}

	//check whether to write stress and strain data to file
	//write stress and strain data to file
	std::string dir(this->userInputs.outputDirectory);
	if(Utilities::MPI::this_mpi_process(this->mpi_communicator)==0){
		dir+="/";
		std::ofstream outputFile;
		dir += std::string("stressstrain.txt");

		if(this->currentIncrement==0){
			outputFile.open(dir.c_str());
			outputFile << "Exx"<<'\t'<<"Eyy"<<'\t'<<"Ezz"<<'\t'<<"Eyz"<<'\t'<<"Exz"<<'\t'<<"Exy"<<'\t'<<"Txx"<<'\t'<<"Tyy"<<'\t'<<"Tzz"<<'\t'<<"Tyz"<<'\t'<<"Txz"<<'\t'<<"Txy"<<'\t'<<"TwinRealVF"<<'\t'<<"TwinMade"<<'\t'<<"SlipTotal"<<'\n';
			outputFile.close();
		}
		outputFile.open(dir.c_str(),std::fstream::app);
		outputFile << global_strain[0][0]<<'\t'<<global_strain[1][1]<<'\t'<<global_strain[2][2]<<'\t'<<global_strain[1][2]<<'\t'<<global_strain[0][2]<<'\t'<<global_strain[0][1]<<'\t'<<global_stress[0][0]<<'\t'<<global_stress[1][1]<<'\t'<<global_stress[2][2]<<'\t'<<global_stress[1][2]<<'\t'<<global_stress[0][2]<<'\t'<<global_stress[0][1]<<'\t'<<F_r<<'\t'<<F_e<<'\t'<<F_s<<'\n';
		outputFile.close();
	}


	//call base class project() function to project post processed fields
	ellipticBVP<dim>::projection();
}


//------------------------------------------------------------------------
template <int dim>
void crystalPlasticity<dim>::rod2quat(Vector<double> &quat,Vector<double> &rod)
{
	double dotrod = rod(0)*rod(0) + rod(1)*rod(1) + rod(2)*rod(2);
	double cphiby2   = cos(atan(sqrt(dotrod)));
	quat(0) = cphiby2;
	quat(1) = cphiby2* rod(0);
	quat(2) = cphiby2* rod(1);
	quat(3) = cphiby2* rod(2);
}
//------------------------------------------------------------------------
template <int dim>
void crystalPlasticity<dim>::quatproduct(Vector<double> &quatp,Vector<double> &quat2,Vector<double> &quat1)
//R(qp) = R(q2)R(q1)
{
	double a = quat2(0);
	double b = quat1(0);
	double dot1 = quat1(1)*quat2(1) + quat1(2)*quat2(2) + quat1(3)*quat2(3);
	quatp(0) = (a*b) - dot1;
	quatp(1) = a*quat1(1) + b*quat2(1)+ quat2(2)*quat1(3) - quat1(2)*quat2(3);
	quatp(2) = a*quat1(2) + b*quat2(2)- quat2(1)*quat1(3) + quat1(1)*quat2(3);
	quatp(3) = a*quat1(3) + b*quat2(3)+ quat2(1)*quat1(2) - quat1(1)*quat2(2);
	if (quatp(0) < 0) {
		quatp(0) = -quatp(0);
		quatp(1) = -quatp(1);
		quatp(2) = -quatp(2);
		quatp(3) = -quatp(3);
	}
}
//------------------------------------------------------------------------
template <int dim>
void crystalPlasticity<dim>::quat2rod(Vector<double> &quat,Vector<double> &rod)
{
	double invquat1 = 1/quat(0);

	for (int i = 0;i <= 2;i++)
	rod(i) = quat(i+1)*invquat1;

}

#include "../../../include/crystalPlasticity_template_instantiations.h"
