import {
	Component,
	OnInit,
	ViewChild
}                               from '@angular/core';
import {
	Router,
	ActivatedRoute
}                               from '@angular/router';

import {
	Hardware,
	HardwareService
}                               from './hardware.service';
import {
	Device, DevicesService
}                               from '../devices/devices.service';
import { DevicesListComponent } from '../devices/list.component';

@Component( {
	templateUrl: 'tpl/hardware-edit.html'
} )

export class HardwareEditComponent implements OnInit {

	public loading: boolean = false;
	public error: String;
	public hardware: Hardware;
	public devices: Device[];

	public hasAdvancedSettings: boolean = false;
	public hasActionDevices: boolean = false;

	@ViewChild('devicesList') private _devicesListComponent: DevicesListComponent;

	public constructor(
		private _router: Router,
		private _route: ActivatedRoute,
		private _hardwareService: HardwareService,
		private _devicesService: DevicesService
	) {
	};

	public ngOnInit() {
		var me = this;
		this._route.data.subscribe( function( data_: any ) {
			me.hardware = data_.hardware;
			me.devices = data_.devices;
			for ( let device of me.devices ) {
				if ( device.subtype == 'action' ) {
					me.hasActionDevices = true;
				}
			}
			for ( let setting of me.hardware.settings ) {
				if ( setting.class == 'advanced' ) {
					me.hasAdvancedSettings = true;
				}
			}
		} );
	};

	public submitHardware() {
		var me = this;
		me.loading = true;
		// TODO figure out how those stupid nested forms work in Angular 2; how tough can it be to just
		// nest two forms???! argh.
		this._hardwareService.putHardware( me.hardware )
			.subscribe(
				function( hardware_: Hardware ) {
					me._router.navigate( [ '/hardware' ] );
				},
				function( error_: string ) {
					me.loading = false;
					me.error = error_;
					window.scrollTo( 0, 0 );
				}
			)
		;
	};

	public deleteHardware() {
		var me = this;
		me.loading = true;
		me._hardwareService.deleteHardware( me.hardware )
			.subscribe(
				function( success_: boolean ) {
					if ( success_ ) {
						me._router.navigate( [ '/hardware' ] );
					} else {
						me.loading = false;
						this.error = 'Unable to delete hardware.';
					}
				},
				function( error_: string ) {
					me.loading = false;
					me.error = error_;
				}
			)
		;
	};

	public performAction( action_: Device ) {
		var me = this;
		me.loading = true;
		this._hardwareService.performAction( me.hardware, action_ )
			.mergeMap( function( success_: boolean ) {
				me.loading = false;
				me._devicesListComponent.loading = true;
				return me._devicesService.getDevices( me.hardware.id );

			} )
			// TODO once the websocket code is in place the device list should update itself, optionally
			// highlighting the row that has changed.
			.delay( new Date( Date.now() + 500 ) )
			.subscribe(
				function( devices_: Device[] ) {
					me._devicesListComponent.devices = devices_;
					me._devicesListComponent.loading = false;
				},
				function( error_: string ) {
					me.error = error_;
				}
			)
		;
	};
}
