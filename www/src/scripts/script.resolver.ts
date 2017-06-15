import { Injectable }      from '@angular/core';
import {
	Router,
	Resolve,
	RouterStateSnapshot,
	ActivatedRouteSnapshot
}                          from '@angular/router';
import { Observable }      from 'rxjs/Observable';
import {
	Script,
	ScriptsService
}                          from './scripts.service';

@Injectable()
export class ScriptResolver implements Resolve<Script> {

	public constructor(
		private _router: Router,
		private _scriptsService: ScriptsService
	) {
	};

	public resolve( route_: ActivatedRouteSnapshot, state_: RouterStateSnapshot ): Observable<Script> {
		if ( route_.params['script_id'] == 'add' ) {
			return Observable.of( { id: NaN, name: 'New script', code: '// enter code here', enabled: true } );
		} else {
			return this._scriptsService.getScript( +route_.params['script_id'] )
				.catch( () => {
					this._router.navigate( [ '/error' ] );
					return Observable.of( null );
				} )
			;
		}
	};

}
