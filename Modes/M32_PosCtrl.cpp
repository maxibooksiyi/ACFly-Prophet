#include "M32_PosCtrl.hpp"
#include "vector3.hpp"
#include "Sensors.hpp"
#include "MeasurementSystem.hpp"
#include "AC_Math.hpp"
#include "Receiver.hpp"
#include "Parameters.hpp"
#include "Commulink.hpp"
#include "ControlSystem.hpp"
#include "StorageSystem.hpp"
#include "Missions.hpp"
#include "NavCmdProcess.hpp"

M32_PosCtrl::M32_PosCtrl():Mode_Base( "PosCtrl", 32 )
{
	
}

ModeResult M32_PosCtrl::main_func( void* param1, uint32_t param2 )
{
	double freq = 50;
	setLedMode(LEDMode_Flying1);
	Altitude_Control_Enable();
	Position_Control_Enable();
	uint16_t exit_mode_counter_rs = 0;
	uint16_t exit_mode_counter = 0;
	uint16_t exit_mode_Gcounter = 0;
	
	//����ģʽ
	bool MissionMode = false;
	bool mode_switched = false;
	uint16_t current_mission_ind;
	MissionInf current_mission_inf;
	double lastMissionButtonValue = -1;
	//����״̬��
	NavCmdInf navInf;
	init_NavCmdInf(&navInf);
	//Zƫ��
	double ZOffset = 0;
	while(1)
	{
		os_delay(0.02);
		
		//��ȡ���ջ�
		Receiver rc;
		getReceiver( &rc, 0, 0.02 );
		
		//���ջ�����
		//���������ֶ�ģʽ�л�
		if( rc.available )
		{
			double MissionButtonValue = rc.data[5];
			if( lastMissionButtonValue < 0 )
				lastMissionButtonValue = MissionButtonValue;
			else if( fabs( MissionButtonValue - lastMissionButtonValue ) > 25 )
			{
				MissionMode = !MissionMode;
				mode_switched = true;
				init_NavCmdInf(&navInf);
				lastMissionButtonValue = MissionButtonValue;
			}
		}
		else
			lastMissionButtonValue = -1;
		
		if( MissionMode )
		{	//����ģʽ
			Position_Control_Enable();
			bool pos_ena;
			is_Position_Control_Enabled(&pos_ena);
			if( pos_ena == false )
			{	//λ�ÿ������޷��򿪷����ֶ�ģʽ
				MissionMode = false;
				goto Manual_Mode;
			}
			
			if( mode_switched )
			{	//���л�������ģʽ
				//����ɲ���ȴ�
				
				if( rc.available )
				{
					bool sticks_in_neutral = 
						in_symmetry_range_mid( rc.data[0] , 50 , 5 ) &&
						in_symmetry_range_mid( rc.data[1] , 50 , 5 ) &&
						in_symmetry_range_mid( rc.data[2] , 50 , 5 ) &&
						in_symmetry_range_mid( rc.data[3] , 50 , 5 );
					if( !sticks_in_neutral )
					{	//ҡ�˲����м䷵���ֶ�ģʽ
						init_NavCmdInf(&navInf);
						MissionMode = false;
						goto Manual_Mode;
					}
				}
				
				Position_Control_set_XYLock();
				Position_Control_set_ZLock();
				
				//�ȴ�ɲ�����
				Position_ControlMode alt_mode, pos_mode;
				get_Altitude_ControlMode(&alt_mode);
				get_Position_ControlMode(&pos_mode);
				if( alt_mode==Position_ControlMode_Position && pos_mode==Position_ControlMode_Position )
				{	//ɲ�����
					++navInf.counter2;
					//�ȴ�1���ٽ����������
					if( navInf.counter2 >= 1*freq )
					{
						mode_switched = false;
						if( ReadCurrentMission(&current_mission_inf, &current_mission_ind) )
						{	//������һ����ɹ�
							//��ʼ��������Ϣ
							init_NavCmdInf(&navInf);
							//����Zƫ��
							current_mission_inf.params[6] += ZOffset;
						}
						else
						{	//��ȡ����������Ϣ
							//�����ŰѺ�������Ϊ�׸�
							setCurrentMission(0);
							if( ReadCurrentMission(&current_mission_inf, &current_mission_ind) )
							{	//������һ����ɹ�
								//��ʼ��������Ϣ
								init_NavCmdInf(&navInf);
								//����Zƫ��
								current_mission_inf.params[6] += ZOffset;
							}
							else
							{	//�޺�����Ϣ�����ֶ�ģʽ
								MissionMode = false;
								goto Manual_Mode;
							}
						}
					}
				}
				else
					navInf.counter2 = 0;
			}
			else
			{	//�������
				if( rc.available )
				{
					bool sticks_in_neutral = 
						in_symmetry_range_mid( rc.data[0] , 50 , 5 ) &&
						in_symmetry_range_mid( rc.data[1] , 50 , 5 ) &&
						in_symmetry_range_mid( rc.data[2] , 50 , 5 ) &&
						in_symmetry_range_mid( rc.data[3] , 50 , 5 );
					if( !sticks_in_neutral )
					{
						init_NavCmdInf(&navInf);
						MissionMode = false;
						goto Manual_Mode;
					}
				}
				
				int16_t res = -3;
				res = Process_NavCmd(
					current_mission_inf.cmd,
					freq, 
					current_mission_inf.frame,
					current_mission_inf.params,
					&navInf
				);							
				
				if( res != -2 )
				{	//�����ִ�����
					
					//���³�ʼ��������Ϣ
					init_NavCmdInf(&navInf);
					
					//���Զ�ִ�з����ֶ�ģʽ
					if( current_mission_inf.autocontinue == 0 )
						MissionMode = false;
					
					if( res < 0 )
					{	//�л�����һģʽ
						MissionInf chk_inf;
						uint16_t chk_ind;
						if( ReadCurrentMission(&chk_inf, &chk_ind) )
						{	//��ȡ��ǰ������Ϣ�Ƚ�						
							if( chk_ind==current_mission_ind && memcmp( &chk_inf, &current_mission_inf, sizeof(MissionInf) ) == 0 )
							{	//�����ͬ���л���һ������
								if( setCurrentMission( getCurrentMissionInd() + 1 ) == false )
								{	//�޺�����Ϣ�����ֶ�ģʽ
									setCurrentMission( 0 );
									MissionMode = false;
								}
								else
								{
									if( ReadCurrentMission(&current_mission_inf, &current_mission_ind) )
									{	//������һ����ɹ�
										//��ʼ��������Ϣ
										init_NavCmdInf(&navInf);
										//����Zƫ��
										current_mission_inf.params[6] += ZOffset;
									}
									else
									{	//�޺�����Ϣ�����ֶ�ģʽ
										setCurrentMission( 0 );
										MissionMode = false;
									}
								}
							}
							else
							{	//������Ϣ����ͬ���л���һ����
								//ʹ���»�ȡ��������Ϣ
								current_mission_inf = chk_inf;
								//����Zƫ��
								current_mission_inf.params[6] += ZOffset;
								//��ʼ��������Ϣ
								init_NavCmdInf(&navInf);
							}
						}
						else
						{	//�޺�����Ϣ�����ֶ�ģʽ
							setCurrentMission( 0 );
							MissionMode = false;
						}
					}
					else
					{	//�л���ָ��ģʽ
						if( setCurrentMission( res ) == false )
						{	//�л�ʧ�ܷ����ֶ�ģʽ
							setCurrentMission( 0 );
							MissionMode = false;			
						}
					}
				}
				else
				{	//����ִ����
					//���Ÿ˲����м��ƶ�zλ��
					if( in_symmetry_range_mid( rc.data[0] , 50 , 5 ) == false )
					{
						double ZIncreament = remove_deadband( rc.data[0] - 50.0 , 5.0 ) * 0.1;
						ZOffset += ZIncreament;
						Position_Control_move_TargetPositionZRelative( ZIncreament );
					}
				}
			}
		}
		else
		{	//�ֶ�����ģʽ���������߶�����ƣ�
			Manual_Mode:
			if( rc.available )
			{				
				/*�ж��˳�ģʽ*/
					//��ȡ����״̬
					bool inFlight;
					get_is_inFlight(&inFlight);
					if( inFlight )
					{
						exit_mode_counter_rs = 400;
						if( exit_mode_counter < exit_mode_counter_rs )
							exit_mode_counter = exit_mode_counter_rs;
					}
					//������Զ�����
					if( inFlight==false && rc.data[0]<30 )
					{
						if( ++exit_mode_counter >= 500 )
						{
							Attitude_Control_Disable();
							return MR_OK;
						}
					}
					else
						exit_mode_counter = exit_mode_counter_rs;
					//���Ƽ���
					if( inFlight==false && (rc.data[0] < 5 && rc.data[1] < 5 && rc.data[2] < 5 && rc.data[3] > 95) )
					{
						if( ++exit_mode_Gcounter >= 50 )
						{
							Attitude_Control_Disable();
							return MR_OK;
						}
					}
					else
						exit_mode_Gcounter = 0;
				/*�ж��˳�ģʽ*/
				
				if( rc.data[4] > 60 )
				{
					Position_Control_Enable();
				}
				else if( rc.data[4] < 40 )
				{
					Position_Control_Disable();
				}
					
				//���Ÿ˿��ƴ�ֱ�ٶ�
				if( in_symmetry_range_mid( rc.data[0] , 50 , 5 ) )
					Position_Control_set_ZLock();
				else
					Position_Control_set_TargetVelocityZ( ( remove_deadband( rc.data[0] - 50.0 , 5.0 ) ) * 6 );
				
				bool pos_ena;
				is_Position_Control_Enabled(&pos_ena);
				if( pos_ena )
				{
					//��������˿�ˮƽ�ٶ�
					if( in_symmetry_range_mid( rc.data[3] , 50 , 5 ) && in_symmetry_range_mid( rc.data[2] , 50 , 5 ) )
						Position_Control_set_XYLock();
					else
					{
						double roll_sitck_d = remove_deadband( rc.data[3] - 50.0, 5.0 );
						double pitch_sitck_d = remove_deadband( rc.data[2] - 50.0, 5.0 );
						Position_Control_set_TargetVelocityBodyHeadingXY_AngleLimit( \
							pitch_sitck_d * 25 ,\
							-roll_sitck_d * 25 , \
							fabs( roll_sitck_d  )*0.017, \
							fabs( pitch_sitck_d )*0.017 \
						);
					}
				}
				else
				{
					//���������Ŷ�
					vector3<double> WindDisturbance;
					get_WindDisturbance( &WindDisturbance );
					Quaternion attitude;
					get_Attitude_quat(&attitude);
					double yaw = attitude.getYaw();		
					double sin_Yaw, cos_Yaw;
					fast_sin_cos( yaw, &sin_Yaw, &cos_Yaw );
					double WindDisturbance_Bodyheading_x = ENU2BodyHeading_x( WindDisturbance.x , WindDisturbance.y , sin_Yaw , cos_Yaw );
					double WindDisturbance_Bodyheading_y = ENU2BodyHeading_y( WindDisturbance.x , WindDisturbance.y , sin_Yaw , cos_Yaw );
					//��������˿ظ������
	//				Attitude_Control_set_Target_RollPitch( 
	//					( rc.data[3] - 50 )*0.015 - atan2(-WindDisturbance_Bodyheading_y , GravityAcc ),
	//					( rc.data[2] - 50 )*0.015 - atan2( WindDisturbance_Bodyheading_x , GravityAcc ) 
	//				);
					Attitude_Control_set_Target_RollPitch( 
						( rc.data[3] - 50 )*0.015,
						( rc.data[2] - 50 )*0.015
					);
				}
				
				//ƫ�������м���ƫ��
				//�����м����ƫ���ٶ�
				if( in_symmetry_range_mid( rc.data[1] , 50 , 5 ) )
					Attitude_Control_set_YawLock();
				else
					Attitude_Control_set_Target_YawRate( ( 50 - rc.data[1] )*0.05 );
			}
			else
			{
				//��ң���źŽ��밲ȫģʽ
				enter_MSafe();
				/*�ж��˳�ģʽ*/
					bool inFlight;
					get_is_inFlight(&inFlight);
					if( inFlight==false )
					{
						Attitude_Control_Disable();
						return MR_OK;
					}
				/*�ж��˳�ģʽ*/
				
			}
		}
	}
	return MR_OK;
}



